//! Bytes that wait in a temporary file instead of memory.
//!
//! The model pass keeps every file's packed resources and artifacts from analysis until publication, over a
//! gigabyte for the whole disc. iOS ends an app long before a desktop runs short, so there they are written to
//! one file in the temporary directory and read back by the job that publishes them.

use super::CoreResult;
use std::borrow::Cow;
use std::fs::File;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};

/// Whether waiting bytes go to the temporary file: on iOS, in the browser (4 GiB of memory, wasm32), and where
/// tests ask for the iOS source path. The browser's game points TMPDIR at its storage while it extracts
/// (port/web/src/firstrun.cpp), so the file is not in memory there either.
const SPILL: bool = cfg!(any(target_os = "ios", target_os = "emscripten", kazarov_mapped_source));

/// What the temporary file has cost: its writes, their bytes and the nanoseconds the writers waited, then the
/// same of its reads.
static IO: [AtomicU64; 6] = [const { AtomicU64::new(0) }; 6];

fn count(first: usize, bytes: usize, started: std::time::Instant) {
    IO[first].fetch_add(1, Ordering::Relaxed);
    IO[first + 1].fetch_add(bytes as u64, Ordering::Relaxed);
    IO[first + 2].fetch_add(started.elapsed().as_nanos() as u64, Ordering::Relaxed);
}

/// The temporary file's writes and reads so far, for a log beside the source's reads; empty when nothing waits
/// in a file.
pub(crate) fn io_text() -> String {
    let io: [u64; 6] = std::array::from_fn(|index| IO[index].load(Ordering::Relaxed));
    if io[0] == 0 {
        return String::new();
    }
    format!(
        "temporary file written {} times, {} MiB, {:.1} s of waiting, and read {} times, {} MiB, {:.1} s",
        io[0],
        io[1] >> 20,
        io[2] as f64 / 1e9,
        io[3],
        io[4] >> 20,
        io[5] as f64 / 1e9
    )
}

pub(crate) enum SpilledBytes {
    Memory(Vec<u8>),
    File { offset: u64, length: usize },
}

impl SpilledBytes {
    pub(crate) fn new(bytes: Vec<u8>) -> CoreResult<Self> {
        if !SPILL || bytes.is_empty() {
            return Ok(Self::Memory(bytes));
        }
        let offset = append(&bytes)?;
        Ok(Self::File {
            offset,
            length: bytes.len(),
        })
    }

    pub(crate) fn read(&self) -> CoreResult<Cow<'_, [u8]>> {
        match self {
            Self::Memory(bytes) => Ok(Cow::Borrowed(bytes)),
            Self::File { offset, length } => {
                let mut bytes = vec![0u8; *length];
                let started = std::time::Instant::now();
                read_at(&spill()?.file, &mut bytes, *offset)?;
                count(3, *length, started);
                Ok(Cow::Owned(bytes))
            }
        }
    }
}

struct Spill {
    file: File,
    /// Serialises appends; reads use positioned I/O and need no lock.
    end: Mutex<u64>,
}

fn spill() -> CoreResult<&'static Spill> {
    static SPILL_FILE: OnceLock<Result<Spill, String>> = OnceLock::new();
    SPILL_FILE
        .get_or_init(|| open().map_err(|error| format!("spill file: {error}")))
        .as_ref()
        .map_err(|error| super::CoreError(error.clone()))
}

fn open() -> std::io::Result<Spill> {
    let path = std::env::temp_dir().join(format!("kazarov-spill-{}.bin", std::process::id()));
    // The browser's storage cannot remove a file that is open, and its process id is always the same: a run
    // that ended early left its file, which goes first; the game removes the directory after extracting.
    #[cfg(target_os = "emscripten")]
    let _ = std::fs::remove_file(&path);
    let mut options = std::fs::OpenOptions::new();
    options.read(true).write(true).create(true).truncate(true);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        // FILE_FLAG_DELETE_ON_CLOSE: the file goes with the process.
        options.custom_flags(0x0400_0000);
    }
    let file = options.open(&path)?;
    // Unlinked while open, so it goes with the process however that ends.
    #[cfg(all(unix, not(target_os = "emscripten")))]
    std::fs::remove_file(&path)?;
    Ok(Spill {
        file,
        end: Mutex::new(0),
    })
}

fn append(bytes: &[u8]) -> CoreResult<u64> {
    let spill = spill()?;
    let mut end = spill
        .end
        .lock()
        .map_err(|_| super::CoreError("spill file lock poisoned".into()))?;
    let offset = *end;
    let started = std::time::Instant::now();
    write_at(&spill.file, bytes, offset)?;
    count(0, bytes.len(), started);
    *end += bytes.len() as u64;
    Ok(offset)
}

#[cfg(unix)]
fn write_at(file: &File, bytes: &[u8], offset: u64) -> std::io::Result<()> {
    use std::os::unix::fs::FileExt;
    file.write_all_at(bytes, offset)
}

#[cfg(unix)]
fn read_at(file: &File, bytes: &mut [u8], offset: u64) -> std::io::Result<()> {
    use std::os::unix::fs::FileExt;
    file.read_exact_at(bytes, offset)
}

#[cfg(windows)]
fn write_at(file: &File, mut bytes: &[u8], mut offset: u64) -> std::io::Result<()> {
    use std::os::windows::fs::FileExt;
    while !bytes.is_empty() {
        let written = file.seek_write(bytes, offset)?;
        if written == 0 {
            return Err(std::io::ErrorKind::WriteZero.into());
        }
        bytes = &bytes[written..];
        offset += written as u64;
    }
    Ok(())
}

#[cfg(windows)]
fn read_at(file: &File, mut bytes: &mut [u8], mut offset: u64) -> std::io::Result<()> {
    use std::os::windows::fs::FileExt;
    while !bytes.is_empty() {
        let read = file.seek_read(bytes, offset)?;
        if read == 0 {
            return Err(std::io::ErrorKind::UnexpectedEof.into());
        }
        bytes = &mut bytes[read..];
        offset += read as u64;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn spilled_bytes_read_back_whole() {
        let first = SpilledBytes::new(vec![1, 2, 3]).unwrap();
        let second = SpilledBytes::new((0..=255).collect()).unwrap();
        assert_eq!(&*first.read().unwrap(), &[1, 2, 3]);
        assert_eq!(second.read().unwrap().len(), 256);
        assert_eq!(second.read().unwrap()[255], 255);
    }
}
