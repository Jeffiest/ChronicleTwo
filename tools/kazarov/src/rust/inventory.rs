//! Immutable retail resource inventory with checked references to nested PACK members.
use super::archive::{ResourceRef, SourceTree};
use super::{CoreError, CoreResult, models};
use rayon::prelude::*;
use std::collections::{BTreeMap, BTreeSet};

fn fail(message: impl Into<String>) -> CoreError {
    CoreError(message.into())
}

/// Above this size a root's first entry header is looked at before all of it is read: the sound archive is a
/// gigabyte and the movies are megabytes to hundreds of megabytes, and that header says they are no PACK.
const HEAD_FIRST: usize = 1 << 20;

/// Whether a root is a sound by its name. The sound archive's three thousand streams are a third of what the
/// roots hold, so their first bytes are looked at before all of them are read, whatever their size.
fn sound_name(alias: &str) -> bool {
    alias
        .rsplit_once('.')
        .is_some_and(|(_, extension)| extension.eq_ignore_ascii_case("wav"))
}

/// Whether a resource's first bytes are a sound stream's, as the sound planner knows one.
fn sound_stream(head: &[u8]) -> bool {
    head.starts_with(b"SShd") || (head.starts_with(b"RIFF") && head.get(8..12) == Some(b"WAVE"))
}

/// A root's bytes hold a texture's signature somewhere: TIM2, IMG, IM2 or IM3.
const TEXTURE_SIGNATURE: u8 = 1;
/// A root's bytes hold a model's signature somewhere: MDS or MDT.
const MODEL_SIGNATURE: u8 = 2;

/// One named PACK member in its original order, including duplicate-name identity.
#[derive(Clone)]
pub struct MemberRef {
    pub alias: String,
    pub name: String,
    pub index: usize,
    pub resource: ResourceRef,
}

/// Top-level disc resources and all nested member references without copied payloads.
pub struct Inventory {
    roots: Vec<ResourceRef>,
    leaves: Vec<ResourceRef>,
    nodes: BTreeMap<String, ResourceRef>,
    members: BTreeMap<String, Vec<MemberRef>>,
    /// The signatures found in each root the index read whole.
    signatures: BTreeMap<String, u8>,
}

/// What one root holds, in the order a walk of it meets them.
#[derive(Default)]
struct Tree {
    nodes: Vec<ResourceRef>,
    leaves: Vec<ResourceRef>,
    members: Vec<(String, Vec<MemberRef>)>,
    signatures: Option<u8>,
}

/// Which of the planners' signatures are anywhere in a root's bytes. Each of them has an M in it, so the bytes are
/// searched for that one letter and looked at around it.
fn signatures(bytes: &[u8]) -> u8 {
    let mut found = 0;
    let mut from = 0;
    while let Some(step) = bytes[from..].iter().position(|&byte| byte == b'M') {
        let at = from + step;
        if at >= 2 && bytes.get(at - 2..at + 2) == Some(b"TIM2") {
            found |= TEXTURE_SIGNATURE;
        }
        if at >= 1
            && let Some(around) = bytes.get(at - 1..at + 3)
            && around[0] == b'I'
            && around[3] == 0
            && matches!(around[2], b'G' | b'2' | b'3')
        {
            found |= TEXTURE_SIGNATURE;
        }
        if let Some(after) = bytes.get(at..at + 4)
            && after[1] == b'D'
            && after[3] == 0
            && matches!(after[2], b'S' | b'T')
        {
            found |= MODEL_SIGNATURE;
        }
        if found == TEXTURE_SIGNATURE | MODEL_SIGNATURE {
            break;
        }
        from = at + 1;
    }
    found
}

impl Tree {
    /// Indexes one root. Its bytes are read once and every member inside it, however deep, is found in them:
    /// read from a disc image a range at a time, each look at a member would be a read of its own, and a PACK
    /// inside a PACK inside a PACK was read once for each of them.
    fn of(mut root: ResourceRef) -> CoreResult<Self> {
        let mut tree = Self::default();
        if root.len() > HEAD_FIRST || sound_name(&root.alias) {
            let head = root.head(models::PACK_HEADER).to_vec();
            let sound = sound_stream(&head);
            if sound || models::no_pack_at(&head) {
                root.note_first(&head);
                // A sound's samples are not searched: nothing in them is a texture or a model, and four of
                // their bytes that happen to spell a signature would only send the stream to be analysed as one.
                if sound {
                    tree.signatures = Some(0);
                }
                tree.nodes.push(root.clone());
                tree.leaves.push(root);
                return Ok(tree);
            }
        }
        let whole = root.clone();
        let bytes = whole.bytes();
        tree.signatures = Some(signatures(&bytes));
        tree.walk(root, &bytes, 0)?;
        Ok(tree)
    }

    fn walk(&mut self, mut resource: ResourceRef, bytes: &[u8], depth: usize) -> CoreResult<ResourceRef> {
        if depth > 16 {
            return Err(fail("PACK nesting exceeds 16"));
        }
        resource.note_first(bytes);
        self.nodes.push(resource.clone());
        let Some(members) = models::pack(bytes) else {
            self.leaves.push(resource.clone());
            return Ok(resource);
        };
        let alias = resource.alias.clone();
        let mut seen = BTreeSet::new();
        let mut children = Vec::with_capacity(members.len());
        for (index, member) in members.into_iter().enumerate() {
            let child_alias = if seen.insert(member.name.clone()) {
                format!("{alias}/{}", member.name)
            } else {
                format!("{alias}/{}#entry:{index}", member.name)
            };
            let end = member
                .start
                .checked_add(member.size)
                .ok_or_else(|| fail("PACK member range overflow"))?;
            let child = resource.subrange(child_alias.clone(), member.start..end)?;
            let inside = bytes
                .get(member.start..end)
                .ok_or_else(|| fail("PACK member outside its PACK"))?;
            let child = self.walk(child, inside, depth + 1)?;
            children.push(MemberRef {
                alias: child_alias,
                name: member.name,
                index,
                resource: child,
            });
        }
        self.members.push((alias, children));
        Ok(resource)
    }
}

impl Inventory {
    pub fn build(source: &SourceTree) -> CoreResult<Self> {
        let trees = source
            .names()
            .map(|name| Tree::of(source.resource(name)?))
            .collect::<CoreResult<Vec<_>>>()?;
        Self::gather(trees)
    }

    /// `build` with the roots indexed on the calling thread pool's workers.
    pub fn build_parallel(source: &SourceTree) -> CoreResult<Self> {
        let names: Vec<&str> = source.names().collect();
        let trees = names
            .par_iter()
            .map(|name| Tree::of(source.resource(name)?))
            .collect::<CoreResult<Vec<_>>>()?;
        Self::gather(trees)
    }

    fn gather(trees: Vec<Tree>) -> CoreResult<Self> {
        let mut inventory = Self {
            roots: Vec::with_capacity(trees.len()),
            leaves: Vec::new(),
            nodes: BTreeMap::new(),
            members: BTreeMap::new(),
            signatures: BTreeMap::new(),
        };
        for tree in trees {
            let root = tree.nodes[0].clone();
            if let Some(found) = tree.signatures {
                inventory.signatures.insert(root.alias.clone(), found);
            }
            inventory.roots.push(root);
            for node in tree.nodes {
                let alias = node.alias.clone();
                if inventory.nodes.insert(alias.clone(), node).is_some() {
                    return Err(fail(format!("duplicate resource identity: {alias}")));
                }
            }
            inventory.leaves.extend(tree.leaves);
            inventory.members.extend(tree.members);
        }
        Ok(inventory)
    }

    /// Whether a root's bytes hold a texture's signature anywhere; None for a root the index did not read whole.
    pub fn texture_signature(&self, root: &str) -> Option<bool> {
        self.signatures
            .get(root)
            .map(|found| found & TEXTURE_SIGNATURE != 0)
    }

    /// Whether a root's bytes hold a model's signature anywhere; None for a root the index did not read whole.
    pub fn model_signature(&self, root: &str) -> Option<bool> {
        self.signatures
            .get(root)
            .map(|found| found & MODEL_SIGNATURE != 0)
    }

    pub fn roots(&self) -> &[ResourceRef] {
        &self.roots
    }

    pub fn leaves(&self) -> &[ResourceRef] {
        &self.leaves
    }

    pub fn get(&self, alias: &str) -> Option<&ResourceRef> {
        self.nodes.get(alias)
    }

    pub fn members(&self, alias: &str) -> Option<&[MemberRef]> {
        self.members.get(alias).map(Vec::as_slice)
    }

    pub fn names(&self) -> impl Iterator<Item = &str> {
        self.nodes.keys().map(String::as_str)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The search the planners made of a file's bytes, four at a time.
    fn searched(bytes: &[u8]) -> u8 {
        let mut found = 0;
        for window in bytes.windows(4) {
            if matches!(window, b"TIM2" | b"IMG\0" | b"IM2\0" | b"IM3\0") {
                found |= TEXTURE_SIGNATURE;
            }
            if matches!(window, b"MDS\0" | b"MDT\0") {
                found |= MODEL_SIGNATURE;
            }
        }
        found
    }

    #[test]
    fn signatures_are_found_as_a_search_of_every_four_bytes_finds_them() {
        let cases: &[&[u8]] = &[
            b"",
            b"M",
            b"TIM",
            b"TIM2",
            b"..TIM2",
            b"IMG",
            b"IMG\0",
            b"IM2\0tail",
            b"..IM3\0",
            b"IM4\0MDX\0",
            b"MDS",
            b"MDS\0",
            b"..MDT\0",
            b"MMDS\0",
            b"TIMDS\0",
            b"IMDT\0IM3\0",
            b"MIMG\0M",
            b"TIM2MDS\0",
        ];
        for case in cases {
            assert_eq!(signatures(case), searched(case), "{case:?}");
        }
        // Every string of the signatures' own letters up to a length, then longer ones picked by a generator.
        let letters = b"TIMDSG23\0x";
        let mut state = 0x2545_f491_4f6c_dd1du64;
        for length in 0..40usize {
            for _ in 0..400 {
                let bytes: Vec<u8> = (0..length)
                    .map(|_| {
                        state ^= state << 13;
                        state ^= state >> 7;
                        state ^= state << 17;
                        letters[(state % letters.len() as u64) as usize]
                    })
                    .collect();
                assert_eq!(signatures(&bytes), searched(&bytes), "{bytes:?}");
            }
        }
    }

    #[test]
    fn a_sound_is_known_by_its_name_and_its_first_bytes() {
        assert!(sound_name("sound/bgm/town.WAV"));
        assert!(!sound_name("chara/c01.chr"));
        assert!(!sound_name("wav"));
        assert!(sound_stream(b"RIFF\x10\0\0\0WAVEfmt "));
        assert!(sound_stream(b"SShd\x18\0\0\0"));
        assert!(!sound_stream(b"RIFF\x10\0\0\0AVI "));
        assert!(!sound_stream(b"RIF"));
    }
}
