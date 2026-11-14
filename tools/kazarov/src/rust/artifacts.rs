//! Bounded in-memory asset submission to one private ZIP writer.
use super::archive::{ResourceRef, SourceSlice};
use super::progress::Phase;
use super::zip_archive::{self, Placed, PreparedEntry, StreamWriter, Written};
use super::{CoreError, CoreResult};
use std::collections::HashSet;
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{SyncSender, TryRecvError, sync_channel};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

fn fail(message: impl Into<String>) -> CoreError {
    CoreError(message.into())
}

/// Final bytes produced by a converter or borrowed directly from the source image.
pub enum ArtifactBytes {
    Owned(Vec<u8>),
    SourceRange(ResourceRef),
}

impl ArtifactBytes {
    pub fn as_slice(&self) -> SourceSlice<'_> {
        match self {
            Self::Owned(bytes) => SourceSlice::Borrowed(bytes),
            Self::SourceRange(resource) => resource.bytes(),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Category {
    Texture,
    Model,
    Audio,
    Movie,
    Script,
    Pack,
    Preserved,
    Gameplay,
}

type JobRunner = Box<dyn FnOnce(&AssetSink) -> CoreResult<()> + Send>;

static PROFILE_NANOS: [std::sync::atomic::AtomicU64; 8] =
    [const { std::sync::atomic::AtomicU64::new(0) }; 8];
static PROFILE_JOBS: [std::sync::atomic::AtomicU64; 8] =
    [const { std::sync::atomic::AtomicU64::new(0) }; 8];

/// With DC2DATA_PROFILE_JOBS set, says on stderr how long the jobs of each category have run so far, summed
/// over the workers.
pub fn profile_note(label: &str) {
    if std::env::var_os("DC2DATA_PROFILE_JOBS").is_none() {
        return;
    }
    static START: std::sync::OnceLock<Instant> = std::sync::OnceLock::new();
    let start = *START.get_or_init(Instant::now);
    let names = [
        "texture", "model", "audio", "movie", "script", "pack", "preserved", "gameplay",
    ];
    let mut line = format!("dc2data profile {:7.2}s {label}:", start.elapsed().as_secs_f64());
    for (index, name) in names.iter().enumerate() {
        let jobs = PROFILE_JOBS[index].load(Ordering::Relaxed);
        if jobs != 0 {
            line.push_str(&format!(
                " {name} {:.1}s/{jobs}",
                PROFILE_NANOS[index].load(Ordering::Relaxed) as f64 / 1e9
            ));
        }
    }
    eprintln!("{line}");
}

/// An entry a job names before it runs.
#[derive(Clone, Debug)]
pub struct Output {
    pub name: String,
    /// The job may end without writing it.
    pub optional: bool,
    /// A picture's width and height, known from planning.
    pub extent: Option<(u32, u32)>,
}

/// A conversion task admitted by byte estimate before its closure runs.
pub struct Job {
    pub category: Category,
    pub label: String,
    pub work: u64,
    pub estimated_bytes: usize,
    /// The entries the job writes, when planning knows them all: a conversion a game reads from while it
    /// runs lists them ahead and runs the job when one is asked for. None for a job that has to run before
    /// anything is read.
    pub outputs: Option<Vec<Output>>,
    pub run: JobRunner,
}

/// Follows a stream as it is written, for a reader of the unfinished archive.
pub trait Observer: Send + Sync {
    /// The file the stream grows in.
    fn opened(&self, partial: &Path);
    /// Entries now readable from that file, in the order written, and the declared jobs that have ended.
    fn published(&self, entries: Vec<Placed>, finished: Vec<usize>);
    /// True while a reader waits, so each entry is handed over as it is written.
    fn hurried(&self) -> bool;
}

/// Flushes what the stream has appended and tells its observer of it.
fn publish(
    writer: &mut StreamWriter,
    observer: &Option<Arc<dyn Observer>>,
    pending: &mut Vec<Placed>,
    finished: &mut Vec<usize>,
    unpublished: &mut u64,
) -> CoreResult<()> {
    let Some(observer) = observer else {
        return Ok(());
    };
    if pending.is_empty() && finished.is_empty() {
        return Ok(());
    }
    writer.flush()?;
    *unpublished = 0;
    observer.published(std::mem::take(pending), std::mem::take(finished));
    Ok(())
}

impl super::jobs::ClassJob for Job {
    fn class(&self) -> usize {
        self.category as usize
    }

    fn work(&self) -> u64 {
        self.work
    }
}

impl Job {
    pub fn execute(self, sink: &AssetSink) -> CoreResult<()> {
        let _admission = sink
            .inner
            .conversion_gate
            .acquire(self.estimated_bytes)
            .map_err(|error| sink.first_failure().unwrap_or(error))?;
        let work = self.work;
        let started = Instant::now();
        let result = (self.run)(sink);
        let elapsed = started.elapsed();
        PROFILE_NANOS[self.category as usize]
            .fetch_add(elapsed.as_nanos() as u64, Ordering::Relaxed);
        PROFILE_JOBS[self.category as usize].fetch_add(1, Ordering::Relaxed);
        if elapsed >= Duration::from_secs(1) && std::env::var_os("DC2DATA_PROFILE_JOBS").is_some() {
            eprintln!(
                "dc2data job {:?} [{}]: {:.3}s",
                self.category,
                self.label,
                elapsed.as_secs_f64()
            );
        }
        if let Err(error) = result {
            let error = CoreError(format!("{:?} [{}]: {error}", self.category, self.label));
            sink.record_failure(&error);
            sink.cancel();
            return Err(error);
        }
        sink.complete(work)
            .map_err(|error| sink.first_failure().unwrap_or(error))
    }
}

struct GateState {
    used: usize,
    limit: usize,
}

struct MemoryGate {
    state: Mutex<GateState>,
    available: Condvar,
    cancelled: AtomicBool,
}

impl MemoryGate {
    fn new(limit: usize) -> Self {
        Self {
            state: Mutex::new(GateState {
                used: 0,
                limit: limit.max(1),
            }),
            available: Condvar::new(),
            cancelled: AtomicBool::new(false),
        }
    }

    fn acquire(self: &Arc<Self>, estimate: usize) -> CoreResult<MemoryPermit> {
        let mut state = self
            .state
            .lock()
            .map_err(|_| fail("asset memory gate poisoned"))?;
        loop {
            if self.cancelled.load(Ordering::Acquire) {
                return Err(fail("asset submission cancelled"));
            }
            let weight = estimate.clamp(1, state.limit);
            if state.used <= state.limit - weight {
                state.used += weight;
                return Ok(MemoryPermit {
                    gate: self.clone(),
                    weight,
                });
            }
            state = self
                .available
                .wait(state)
                .map_err(|_| fail("asset memory gate poisoned"))?;
        }
    }

    fn cancel(&self) {
        self.cancelled.store(true, Ordering::Release);
        self.available.notify_all();
    }

    fn limit(&self) -> CoreResult<usize> {
        Ok(self
            .state
            .lock()
            .map_err(|_| fail("asset memory gate poisoned"))?
            .limit)
    }

    fn raise_limit(&self, new_limit: usize) -> CoreResult<usize> {
        let mut state = self
            .state
            .lock()
            .map_err(|_| fail("asset memory gate poisoned"))?;
        if new_limit > state.limit {
            state.limit = new_limit;
            self.available.notify_all();
        }
        Ok(state.limit)
    }
}

struct MemoryPermit {
    gate: Arc<MemoryGate>,
    weight: usize,
}

impl Drop for MemoryPermit {
    fn drop(&mut self) {
        if let Ok(mut state) = self.gate.state.lock() {
            state.used -= self.weight;
            self.gate.available.notify_all();
        }
    }
}

enum Message {
    Entry(PreparedEntry, MemoryPermit, Category),
    Completed(u64),
    /// A declared job has written everything it will.
    JobDone(usize),
    /// Answered once everything sent before it is in the file and its observer knows.
    Barrier(std::sync::mpsc::Sender<()>),
    Finish,
    Abort,
}

struct SinkInner {
    sender: SyncSender<Message>,
    writer: Mutex<Option<JoinHandle<CoreResult<Written>>>>,
    names: Mutex<HashSet<String>>,
    conversion_gate: Arc<MemoryGate>,
    queue_gate: Arc<MemoryGate>,
    first_failure: Arc<Mutex<Option<String>>>,
}

/// Clonable producer handle for a single bounded, unpublished ZIP stream.
#[derive(Clone)]
pub struct AssetSink {
    inner: Arc<SinkInner>,
}

impl AssetSink {
    pub fn conversion_limit(&self) -> CoreResult<usize> {
        self.inner.conversion_gate.limit()
    }

    pub fn queue_limit(&self) -> CoreResult<usize> {
        self.inner.queue_gate.limit()
    }

    /// Lets remaining conversion jobs use available memory after other producers finish.
    pub fn raise_conversion_limit(&self, new_limit: usize) -> CoreResult<usize> {
        self.inner.conversion_gate.raise_limit(new_limit)
    }

    /// Runs planning work under the same byte admission used by conversion jobs.
    pub(crate) fn with_admission<T>(
        &self,
        estimated_bytes: usize,
        body: impl FnOnce() -> CoreResult<T>,
    ) -> CoreResult<T> {
        let _admission = self.inner.conversion_gate.acquire(estimated_bytes)?;
        let result = body();
        if let Err(error) = &result {
            self.record_failure(error);
            self.cancel();
        }
        result
    }

    pub fn start(stage: &Path, budget_bytes: usize) -> CoreResult<Self> {
        Self::start_with_progress(stage, budget_bytes, Phase::noop(), 0, None)
    }

    pub fn start_with_progress(
        stage: &Path,
        budget_bytes: usize,
        phase: Phase,
        total_work: u64,
        observer: Option<Arc<dyn Observer>>,
    ) -> CoreResult<Self> {
        std::fs::create_dir_all(stage)?;
        let writer = StreamWriter::new(&stage.join("data.zip"))?;
        if let Some(observer) = &observer {
            observer.opened(writer.partial());
        }
        if total_work != 0 {
            phase.set_total(total_work);
        }
        let (sender, receiver) = sync_channel::<Message>(64);
        let queue_gate = Arc::new(MemoryGate::new(budget_bytes));
        let conversion_gate = Arc::new(MemoryGate::new(budget_bytes));
        let worker_gate = queue_gate.clone();
        let worker_conversion_gate = conversion_gate.clone();
        let first_failure = Arc::new(Mutex::new(None));
        let worker_failure = first_failure.clone();
        let handle = std::thread::Builder::new()
            .name("dc2data-zip-writer".into())
            .spawn(move || {
                let result = (|| -> CoreResult<Written> {
                    let mut writer = writer;
                    // An observed stream hands over what it has written whenever it has nothing to
                    // write, at each megabyte, and at once while a reader waits.
                    let mut pending = Vec::new();
                    let mut finished = Vec::new();
                    let mut unpublished = 0u64;
                    loop {
                        let message = match receiver.try_recv() {
                            Ok(message) => message,
                            Err(TryRecvError::Empty) => {
                                publish(
                                    &mut writer,
                                    &observer,
                                    &mut pending,
                                    &mut finished,
                                    &mut unpublished,
                                )?;
                                match receiver.recv() {
                                    Ok(message) => message,
                                    Err(_) => break,
                                }
                            }
                            Err(TryRecvError::Disconnected) => break,
                        };
                        let mut hand_over = false;
                        match message {
                            Message::Entry(entry, _permit, _category) => {
                                let placed = writer.append(entry)?;
                                phase.add_bytes(placed.written);
                                if let Some(observer) = &observer {
                                    unpublished += placed.written;
                                    pending.push(placed);
                                    hand_over = unpublished >= 1 << 20 || observer.hurried();
                                }
                            }
                            Message::Completed(work) => phase.inc(work),
                            Message::JobDone(job) => {
                                finished.push(job);
                                hand_over = observer.as_ref().is_some_and(|o| o.hurried());
                            }
                            Message::Barrier(reply) => {
                                publish(
                                    &mut writer,
                                    &observer,
                                    &mut pending,
                                    &mut finished,
                                    &mut unpublished,
                                )?;
                                let _ = reply.send(());
                            }
                            Message::Finish => {
                                publish(
                                    &mut writer,
                                    &observer,
                                    &mut pending,
                                    &mut finished,
                                    &mut unpublished,
                                )?;
                                let written = writer.finish()?;
                                return Ok(written);
                            }
                            Message::Abort => return Err(fail("ZIP stream aborted")),
                        }
                        if hand_over {
                            publish(
                                &mut writer,
                                &observer,
                                &mut pending,
                                &mut finished,
                                &mut unpublished,
                            )?;
                        }
                    }
                    Err(fail("ZIP writer stopped before finish"))
                })();
                if let Err(error) = &result {
                    if let Ok(mut first) = worker_failure.lock() {
                        first.get_or_insert_with(|| error.to_string());
                    }
                    worker_gate.cancel();
                    worker_conversion_gate.cancel();
                }
                result
            })?;
        Ok(Self {
            inner: Arc::new(SinkInner {
                sender,
                writer: Mutex::new(Some(handle)),
                names: Mutex::new(HashSet::new()),
                conversion_gate,
                queue_gate,
                first_failure,
            }),
        })
    }

    fn record_failure(&self, error: &CoreError) {
        if error.0 == "asset submission cancelled" {
            return;
        }
        if let Ok(mut first) = self.inner.first_failure.lock() {
            first.get_or_insert_with(|| error.to_string());
        }
    }

    /// First underlying producer or writer failure, retained across parallel cancellation.
    pub fn first_failure(&self) -> Option<CoreError> {
        self.inner
            .first_failure
            .lock()
            .ok()
            .and_then(|failure| failure.clone().map(CoreError))
    }

    pub fn submit(
        &self,
        name: impl Into<String>,
        bytes: ArtifactBytes,
        category: Category,
    ) -> CoreResult<()> {
        let name = name.into();
        if category == Category::Gameplay && super::configs::gameplay_digest(&name).is_none() {
            let error = fail(format!(
                "{category:?} [{name}]: gameplay document is not tracked"
            ));
            self.record_failure(&error);
            self.cancel();
            return Err(error);
        }
        zip_archive::validate_name(&name)?;
        let lower = name.to_ascii_lowercase();
        if category != Category::Gameplay
            && (lower.starts_with("gameplay/")
                || super::configs::is_tracked_gameplay(&lower)
                || lower.ends_with(".cfg.json")
                || lower.ends_with(".map.json")
                || lower.ends_with(".character.json")
                || lower.ends_with(".sound/config.json")
                || lower.ends_with(".sound/volume.json")
                || matches!(lower.as_str(), "snd2/rev.json" | "snd2/map_index.json"))
        {
            let error = fail(format!(
                "{category:?} [{name}]: gameplay config requires the Gameplay category"
            ));
            self.record_failure(&error);
            self.cancel();
            return Err(error);
        }
        {
            let mut names = self
                .inner
                .names
                .lock()
                .map_err(|_| fail("asset names lock poisoned"))?;
            if !names.insert(lower) {
                let error = fail(format!(
                    "{category:?} [{name}]: archive entry names collide ignoring case"
                ));
                self.record_failure(&error);
                self.cancel();
                return Err(error);
            }
        }
        let estimate = bytes
            .as_slice()
            .len()
            .saturating_mul(2)
            .saturating_add(1 << 16);
        let permit = self.inner.queue_gate.acquire(estimate)?;
        let entry_name = name.clone();
        let prepared = match zip_archive::prepare_artifact(name, bytes) {
            Ok(prepared) => prepared,
            Err(error) => {
                self.record_failure(&fail(format!("{category:?} [{entry_name}]: {error}")));
                self.cancel();
                return Err(error);
            }
        };
        self.inner
            .sender
            .send(Message::Entry(prepared, permit, category))
            .map_err(|_| {
                self.cancel();
                fail("ZIP writer stopped during submission")
            })
    }

    pub fn cancel(&self) {
        self.inner.conversion_gate.cancel();
        self.inner.queue_gate.cancel();
    }

    pub fn complete(&self, work: u64) -> CoreResult<()> {
        self.inner
            .sender
            .send(Message::Completed(work))
            .map_err(|_| {
                self.cancel();
                fail("ZIP writer stopped before completing a job")
            })
    }

    /// Tells the stream's observer that a declared job has written everything it will. Sent by the job's
    /// own thread after its entries, so it reaches the observer after them.
    pub fn finished(&self, job: usize) -> CoreResult<()> {
        self.inner.sender.send(Message::JobDone(job)).map_err(|_| {
            self.cancel();
            self.first_failure()
                .unwrap_or_else(|| fail("ZIP writer stopped before a job ended"))
        })
    }

    /// Waits until everything submitted so far is in the file and the stream's observer has been told.
    pub fn barrier(&self) -> CoreResult<()> {
        let (reply, done) = std::sync::mpsc::channel();
        self.inner
            .sender
            .send(Message::Barrier(reply))
            .map_err(|_| fail("ZIP writer stopped before a barrier"))?;
        done.recv().map_err(|_| {
            self.first_failure()
                .unwrap_or_else(|| fail("ZIP writer stopped at a barrier"))
        })
    }

    pub fn finish(self) -> CoreResult<Written> {
        self.inner
            .sender
            .send(Message::Finish)
            .map_err(|_| fail("ZIP writer stopped before finish"))?;
        let handle = self
            .inner
            .writer
            .lock()
            .map_err(|_| fail("ZIP writer lock poisoned"))?
            .take()
            .ok_or_else(|| fail("ZIP writer already finished"))?;
        handle.join().map_err(|_| fail("ZIP writer panicked"))?
    }

    /// Stops a failed conversion and waits until the unpublished writer releases its file.
    pub fn abort(self) -> CoreResult<()> {
        self.cancel();
        let _ = self.inner.sender.send(Message::Abort);
        if let Some(handle) = self
            .inner
            .writer
            .lock()
            .map_err(|_| fail("ZIP writer lock poisoned"))?
            .take()
        {
            let _ = handle.join().map_err(|_| fail("ZIP writer panicked"))?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::MemoryGate;
    use std::sync::Arc;
    use std::sync::mpsc::channel;
    use std::time::Duration;

    #[test]
    fn raising_conversion_limit_wakes_a_waiting_job() {
        let gate = Arc::new(MemoryGate::new(10));
        let first = gate.acquire(10).unwrap();
        let (sender, receiver) = channel();
        let waiting = gate.clone();
        let worker = std::thread::spawn(move || {
            let _permit = waiting.acquire(10).unwrap();
            sender.send(()).unwrap();
        });
        assert!(receiver.recv_timeout(Duration::from_millis(50)).is_err());
        assert_eq!(gate.raise_limit(20).unwrap(), 20);
        receiver.recv_timeout(Duration::from_secs(2)).unwrap();
        worker.join().unwrap();
        drop(first);
    }
}
