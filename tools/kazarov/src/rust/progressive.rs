//! A conversion the game reads from while it runs.
//!
//! The run is planned whole before any job starts, so each output's name is known long before its bytes are.
//! Once planning and the jobs that cannot name their outputs ahead are done the session is ready: it lists
//! every name, and a read of one whose job has not run yet moves that job to the front and waits for it.
use super::artifacts::{AssetSink, Job, Observer};
use super::zip_archive::{self, Placed};
use super::{CoreError, CoreResult};
use std::collections::{HashMap, HashSet, VecDeque};
use std::fs::File;
use std::path::Path;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Condvar, Mutex, MutexGuard, RwLock, RwLockWriteGuard};

/// The share of a whole run's progress at which the European disc becomes readable, measured natively. A
/// host scales its "preparing" bar by it, so the bar ends where play begins.
pub const READY_FRACTION: f32 = 0.40;

fn fail(message: impl Into<String>) -> CoreError {
    CoreError(message.into())
}

fn folder(name: &str) -> &str {
    name.rsplit_once('/').map_or("", |(folder, _)| folder)
}

/// The first 24 bytes of a PNG of this size: its signature and the start of its header chunk.
fn png_header(width: u32, height: u32) -> [u8; 24] {
    let mut header = [0u8; 24];
    header[..8].copy_from_slice(&[0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a]);
    header[8..12].copy_from_slice(&13u32.to_be_bytes());
    header[12..16].copy_from_slice(b"IHDR");
    header[16..20].copy_from_slice(&width.to_be_bytes());
    header[20..24].copy_from_slice(&height.to_be_bytes());
    header
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Phase {
    /// Planning, and the jobs whose outputs are not known ahead. Nothing can be read yet.
    Preparing,
    /// Every name is listed. A read waits for its job if that has not run.
    Ready,
    /// The archive is whole and published.
    Sealed,
    Failed,
}

pub struct Status {
    pub phase: Phase,
    /// The whole run's progress, 0 to 1, and the stage it is in.
    pub fraction: f32,
    pub stage: &'static str,
    /// Declared jobs done and in all, once ready.
    pub done: usize,
    pub total: usize,
    /// Entries written that no job had declared, and declared names whose job ended without them.
    pub undeclared: u64,
    pub missing: u64,
    pub error: String,
}

enum Slot {
    Pending {
        job: usize,
        optional: bool,
        extent: Option<(u32, u32)>,
    },
    Written(Placed),
    Absent,
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum Turn {
    Queued,
    Running,
    Done,
}

struct State {
    phase: Phase,
    error: String,
    fraction: f32,
    stage: &'static str,
    /// Entries written before the names were frozen.
    early: Vec<Placed>,
    names: Vec<String>,
    slots: Vec<Slot>,
    index: HashMap<String, usize>,
    jobs: Vec<Option<Job>>,
    turns: Vec<Turn>,
    /// 0 in the background order, 1 moved up with its folder, 2 asked for by a reader.
    rank: Vec<u8>,
    outputs: Vec<Vec<usize>>,
    urgent: VecDeque<usize>,
    soon: VecDeque<usize>,
    cursor: usize,
    folders: HashMap<String, Vec<usize>>,
    promoted: HashSet<String>,
    done: usize,
    undeclared: u64,
    missing: u64,
}

impl State {
    /// Moves the job of a pending entry to the front, and the jobs of its folder behind it: a scene that
    /// wants one file of a folder soon wants the rest.
    fn hurry(&mut self, index: usize) {
        let Slot::Pending { job, .. } = self.slots[index] else {
            return;
        };
        if self.turns[job] == Turn::Queued && self.rank[job] < 2 {
            self.rank[job] = 2;
            self.urgent.push_back(job);
        }
        let folder = folder(&self.names[index]).to_owned();
        if !self.promoted.contains(&folder) {
            if let Some(jobs) = self.folders.get(&folder) {
                for &job in jobs {
                    if self.turns[job] == Turn::Queued && self.rank[job] < 1 {
                        self.rank[job] = 1;
                        self.soon.push_back(job);
                    }
                }
            }
            self.promoted.insert(folder);
        }
    }

    fn take(&mut self) -> Option<(usize, Job)> {
        loop {
            let id = if let Some(id) = self.urgent.pop_front() {
                id
            } else if let Some(id) = self.soon.pop_front() {
                id
            } else if self.cursor < self.jobs.len() {
                self.cursor += 1;
                self.cursor - 1
            } else {
                return None;
            };
            if self.turns[id] == Turn::Queued {
                if let Some(job) = self.jobs[id].take() {
                    self.turns[id] = Turn::Running;
                    return Some((id, job));
                }
            }
        }
    }
}

pub struct Session {
    state: Mutex<State>,
    changed: Condvar,
    /// The growing archive, then the published one. Held for writing, and empty, while it is renamed.
    reader: RwLock<Option<File>>,
    waiting: AtomicUsize,
}

struct Waiting<'a>(&'a AtomicUsize);

impl Drop for Waiting<'_> {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::AcqRel);
    }
}

/// Reads stopped while the archive file is closed for a rename.
pub struct Suspended<'a> {
    session: &'a Session,
    guard: Option<RwLockWriteGuard<'a, Option<File>>>,
}

impl Suspended<'_> {
    /// Opens the archive where it now is and lets reads go on.
    pub fn resume(mut self, path: &Path) -> CoreResult<()> {
        let file = File::open(path)
            .map_err(|error| fail(format!("{}: {error}", path.display())))?;
        if let Some(mut guard) = self.guard.take() {
            *guard = Some(file);
        }
        Ok(())
    }
}

impl Drop for Suspended<'_> {
    fn drop(&mut self) {
        if self.guard.take().is_some() {
            self.session
                .fail("the conversion stopped before its archive was published");
        }
    }
}

impl Default for Session {
    fn default() -> Self {
        Self::new()
    }
}

impl Session {
    pub fn new() -> Self {
        Self {
            state: Mutex::new(State {
                phase: Phase::Preparing,
                error: String::new(),
                fraction: 0.0,
                stage: "",
                early: Vec::new(),
                names: Vec::new(),
                slots: Vec::new(),
                index: HashMap::new(),
                jobs: Vec::new(),
                turns: Vec::new(),
                rank: Vec::new(),
                outputs: Vec::new(),
                urgent: VecDeque::new(),
                soon: VecDeque::new(),
                cursor: 0,
                folders: HashMap::new(),
                promoted: HashSet::new(),
                done: 0,
                undeclared: 0,
                missing: 0,
            }),
            changed: Condvar::new(),
            reader: RwLock::new(None),
            waiting: AtomicUsize::new(0),
        }
    }

    fn lock(&self) -> CoreResult<MutexGuard<'_, State>> {
        self.state
            .lock()
            .map_err(|_| fail("progressive conversion state poisoned"))
    }

    pub fn progress(&self, fraction: f32, stage: &'static str) {
        if let Ok(mut state) = self.state.lock() {
            state.fraction = fraction;
            state.stage = stage;
        }
    }

    pub fn status(&self) -> Status {
        match self.state.lock() {
            Ok(state) => Status {
                phase: state.phase,
                fraction: state.fraction,
                stage: state.stage,
                done: state.done,
                total: state.jobs.len(),
                undeclared: state.undeclared,
                missing: state.missing,
                error: state.error.clone(),
            },
            Err(_) => Status {
                phase: Phase::Failed,
                fraction: 0.0,
                stage: "",
                done: 0,
                total: 0,
                undeclared: 0,
                missing: 0,
                error: "progressive conversion state poisoned".into(),
            },
        }
    }

    /// Ends the session with an error; every waiting read returns it.
    pub fn fail(&self, message: &str) {
        if let Ok(mut state) = self.state.lock() {
            if !matches!(state.phase, Phase::Failed | Phase::Sealed) {
                state.phase = Phase::Failed;
                state.error = message.to_owned();
            }
        }
        self.changed.notify_all();
    }

    /// Marks the archive whole and published.
    pub fn sealed(&self) {
        if let Ok(mut state) = self.state.lock() {
            if state.phase == Phase::Ready {
                state.phase = Phase::Sealed;
            }
        }
        self.changed.notify_all();
    }

    /// Freezes the names: those written so far and those the remaining jobs declare. `jobs` run in this
    /// order unless a reader asks for one sooner.
    pub fn ready(&self, jobs: Vec<Job>) -> CoreResult<()> {
        let mut state = self.lock()?;
        if state.phase != Phase::Preparing {
            return Err(fail("a progressive conversion is made ready once"));
        }
        let mut names: Vec<String> = state
            .early
            .iter()
            .map(|placed| placed.name.clone())
            .collect();
        for job in &jobs {
            names.extend(
                job.outputs
                    .iter()
                    .flatten()
                    .map(|output| output.name.clone()),
            );
        }
        names.sort_unstable();
        names.dedup();
        let index: HashMap<String, usize> = names
            .iter()
            .enumerate()
            .map(|(at, name)| (name.clone(), at))
            .collect();
        let mut slots: Vec<Slot> = names.iter().map(|_| Slot::Absent).collect();
        for placed in std::mem::take(&mut state.early) {
            let at = index[&placed.name];
            slots[at] = Slot::Written(placed);
        }
        let mut outputs = vec![Vec::new(); jobs.len()];
        let mut folders: HashMap<String, Vec<usize>> = HashMap::new();
        for (id, job) in jobs.iter().enumerate() {
            for output in job.outputs.iter().flatten() {
                let at = index[&output.name];
                if !matches!(slots[at], Slot::Absent) {
                    state.undeclared += 1;
                    if state.undeclared <= 8 {
                        eprintln!("dc2data: output declared twice: {}", output.name);
                    }
                    continue;
                }
                slots[at] = Slot::Pending {
                    job: id,
                    optional: output.optional,
                    extent: output.extent,
                };
                outputs[id].push(at);
                let list = folders.entry(folder(&output.name).to_owned()).or_default();
                if list.last() != Some(&id) {
                    list.push(id);
                }
            }
        }
        state.names = names;
        state.slots = slots;
        state.index = index;
        state.turns = vec![Turn::Queued; jobs.len()];
        state.rank = vec![0; jobs.len()];
        state.jobs = jobs.into_iter().map(Some).collect();
        state.outputs = outputs;
        state.folders = folders;
        state.phase = Phase::Ready;
        drop(state);
        self.changed.notify_all();
        Ok(())
    }

    /// One worker's loop: runs declared jobs, a reader's first, until none is left.
    pub fn work(
        &self,
        sink: &AssetSink,
        run: &(dyn Fn(Job, &AssetSink) -> CoreResult<()> + Sync),
    ) -> CoreResult<()> {
        loop {
            let (id, job) = {
                let mut state = self.lock()?;
                if state.phase == Phase::Failed {
                    return Err(fail(state.error.clone()));
                }
                match state.take() {
                    Some(next) => next,
                    None => return Ok(()),
                }
            };
            if let Err(error) = run(job, sink) {
                self.fail(&error.to_string());
                return Err(error);
            }
            sink.finished(id)?;
        }
    }

    /// Stops reads and closes the archive file, so the file can be renamed.
    pub fn suspend(&self) -> CoreResult<Suspended<'_>> {
        let mut guard = self
            .reader
            .write()
            .map_err(|_| fail("progressive archive reader poisoned"))?;
        *guard = None;
        Ok(Suspended {
            session: self,
            guard: Some(guard),
        })
    }

    pub fn count(&self) -> usize {
        self.state.lock().map_or(0, |state| state.names.len())
    }

    pub fn name(&self, index: usize) -> CoreResult<String> {
        self.lock()?
            .names
            .get(index)
            .cloned()
            .ok_or_else(|| fail("no such progressive entry"))
    }

    /// Where the entry lies, waiting for its job if that has not written it; None when the job ended
    /// without it.
    fn settle(&self, index: usize) -> CoreResult<Option<Placed>> {
        let mut state = self.lock()?;
        let mut waiting = None;
        loop {
            if state.phase == Phase::Failed {
                return Err(fail(state.error.clone()));
            }
            match state.slots.get(index) {
                None => return Err(fail("no such progressive entry")),
                Some(Slot::Written(placed)) => return Ok(Some(placed.clone())),
                Some(Slot::Absent) => return Ok(None),
                Some(Slot::Pending { .. }) => {}
            }
            if waiting.is_none() {
                state.hurry(index);
                self.waiting.fetch_add(1, Ordering::AcqRel);
                waiting = Some(Waiting(&self.waiting));
            }
            state = self
                .changed
                .wait(state)
                .map_err(|_| fail("progressive conversion state poisoned"))?;
        }
    }

    /// Whether the entry exists. Waits only for a name its job may leave out.
    pub fn present(&self, index: usize) -> CoreResult<bool> {
        {
            let state = self.lock()?;
            match state.slots.get(index) {
                None => return Ok(false),
                Some(Slot::Written(_)) => return Ok(true),
                Some(Slot::Absent) => return Ok(false),
                Some(Slot::Pending { optional, .. }) => {
                    if !optional {
                        return Ok(true);
                    }
                }
            }
        }
        Ok(self.settle(index)?.is_some())
    }

    pub fn size(&self, index: usize) -> CoreResult<u64> {
        match self.settle(index)? {
            Some(placed) => Ok(placed.size),
            None => Err(fail(format!(
                "{}: not made by the conversion",
                self.name(index)?
            ))),
        }
    }

    fn with_file<T>(&self, body: impl FnOnce(&File) -> CoreResult<T>) -> CoreResult<T> {
        let reader = self
            .reader
            .read()
            .map_err(|_| fail("progressive archive reader poisoned"))?;
        match reader.as_ref() {
            Some(file) => body(file),
            None => Err(fail("the progressive archive is not open")),
        }
    }

    /// Reads a whole entry, converting it first if its turn had not come.
    pub fn read(&self, index: usize, out: &mut [u8]) -> CoreResult<()> {
        let Some(placed) = self.settle(index)? else {
            return Err(fail(format!(
                "{}: not made by the conversion",
                self.name(index)?
            )));
        };
        self.with_file(|file| zip_archive::read_placed(file, &placed, out))
    }

    /// Reads the start of an entry and returns how many bytes it gave. The header of a picture not
    /// converted yet is made from its planned size: a scene asks the size of far more pictures than it
    /// draws, and none of them has to be converted for that.
    pub fn prefix(&self, index: usize, out: &mut [u8]) -> CoreResult<usize> {
        {
            let state = self.lock()?;
            if let Some(Slot::Pending {
                extent: Some((width, height)),
                ..
            }) = state.slots.get(index)
            {
                if out.len() <= 24 {
                    let header = png_header(*width, *height);
                    let count = out.len();
                    out.copy_from_slice(&header[..count]);
                    return Ok(count);
                }
            }
        }
        let Some(placed) = self.settle(index)? else {
            return Err(fail(format!(
                "{}: not made by the conversion",
                self.name(index)?
            )));
        };
        let count = out.len().min(placed.size as usize);
        if placed.method == zip_archive::METHOD_STORED {
            self.with_file(|file| zip_archive::read_placed_prefix(file, &placed, &mut out[..count]))?;
        } else {
            let mut whole = vec![0u8; placed.size as usize];
            self.with_file(|file| zip_archive::read_placed(file, &placed, &mut whole))?;
            out[..count].copy_from_slice(&whole[..count]);
        }
        Ok(count)
    }

    /// Checks what the session listed against the published archive: the same names, each at the place
    /// and with the size and CRC the session reads it by.
    pub fn compare(&self, archive: &zip_archive::Archive) -> CoreResult<String> {
        let state = self.lock()?;
        let listed: HashMap<&str, usize> = (0..archive.len())
            .map(|at| (archive.name(at), at))
            .collect();
        let mut written = 0usize;
        let mut absent = 0usize;
        let mut wrong = Vec::new();
        for (index, slot) in state.slots.iter().enumerate() {
            let name = state.names[index].as_str();
            match slot {
                Slot::Written(placed) => {
                    written += 1;
                    let same = match listed.get(name) {
                        Some(&at) => {
                            archive.size(at) == placed.size
                                && archive.crc(at) == placed.crc
                                && archive.data_offset(at)? == placed.data
                        }
                        None => false,
                    };
                    if !same {
                        wrong.push(format!("{name} (differs from the archive)"));
                    }
                }
                Slot::Absent => {
                    absent += 1;
                    if listed.contains_key(name) {
                        wrong.push(format!("{name} (in the archive, absent in the session)"));
                    }
                }
                Slot::Pending { .. } => wrong.push(format!("{name} (never settled)")),
            }
        }
        if archive.len() != written
            || !wrong.is_empty()
            || state.undeclared != 0
            || state.missing != 0
        {
            wrong.truncate(8);
            return Err(fail(format!(
                "progressive listing differs from the archive: {} archive entries, {written} written, \
                 {} undeclared, {} declared and not written; {}",
                archive.len(),
                state.undeclared,
                state.missing,
                wrong.join(", ")
            )));
        }
        Ok(format!(
            "{written} entries as listed, {absent} optional names absent, none undeclared"
        ))
    }
}

impl Observer for Session {
    fn opened(&self, partial: &Path) {
        match File::open(partial) {
            Ok(file) => {
                if let Ok(mut reader) = self.reader.write() {
                    *reader = Some(file);
                }
            }
            Err(error) => self.fail(&format!("{}: {error}", partial.display())),
        }
    }

    fn published(&self, entries: Vec<Placed>, finished: Vec<usize>) {
        let Ok(mut state) = self.state.lock() else {
            return;
        };
        for placed in entries {
            if state.phase == Phase::Preparing {
                state.early.push(placed);
                continue;
            }
            match state.index.get(&placed.name).copied() {
                Some(at) => state.slots[at] = Slot::Written(placed),
                None => {
                    state.undeclared += 1;
                    if state.undeclared <= 8 {
                        eprintln!("dc2data: entry written without being declared: {}", placed.name);
                    }
                }
            }
        }
        for job in finished {
            if job >= state.turns.len() {
                continue;
            }
            for position in 0..state.outputs[job].len() {
                let at = state.outputs[job][position];
                if let Slot::Pending { optional, .. } = state.slots[at] {
                    if !optional {
                        state.missing += 1;
                        if state.missing <= 8 {
                            eprintln!("dc2data: declared output not written: {}", state.names[at]);
                        }
                    }
                    state.slots[at] = Slot::Absent;
                }
            }
            if state.turns[job] != Turn::Done {
                state.turns[job] = Turn::Done;
                state.done += 1;
            }
        }
        drop(state);
        self.changed.notify_all();
    }

    fn hurried(&self) -> bool {
        self.waiting.load(Ordering::Acquire) != 0
    }
}
