#include "../sound_stream_data.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
static void U16(Bytes& b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void U32(Bytes& b, unsigned v) { for (int i=0;i<4;++i) b.push_back((v >> (8*i)) & 255); }
static void Patch32(Bytes& b, std::size_t p, unsigned v) { for(int i=0;i<4;++i)b[p+i]=(v>>(8*i))&255; }
static void Four(Bytes& b, const char* s) { b.insert(b.end(),s,s+4); }
static void Chunk(Bytes& riff, const char* id, const Bytes& payload) {
    Four(riff,id); U32(riff,static_cast<unsigned>(payload.size())); riff.insert(riff.end(),payload.begin(),payload.end());
    if(payload.size()&1) riff.push_back(0);
}
static Bytes Wave() {
    Bytes r; Four(r,"RIFF"); U32(r,0); Four(r,"WAVE");
    Chunk(r,"JUNK",Bytes{7}); // odd chunk and pad
    Bytes pcm; U16(pcm,1); U16(pcm,1); U32(pcm,48000); U32(pcm,96000); U16(pcm,2); U16(pcm,16);
    Chunk(r,"fmt ",pcm); Chunk(r,"data",Bytes{0x34,0x12,0xfe,0xff});
    Patch32(r,4,static_cast<unsigned>(r.size()-8)); return r;
}
static void WriteIndex(const fs::path& root, const std::vector<std::string>& names, const std::vector<unsigned>& sectors,
                       const std::vector<unsigned>& sizes) {
    const unsigned start=static_cast<unsigned>((names.size()+1)*16); Bytes h;
    unsigned n=start;
    for(std::size_t i=0;i<names.size();++i){ U32(h,n); U32(h,sizes[i]); U32(h,sectors[i]); U32(h,(sizes[i]+2047)/2048); n+=static_cast<unsigned>(names[i].size()+1); }
    U32(h,n); U32(h,0); U32(h,0); U32(h,0);
    for(const auto& name:names){h.insert(h.end(),name.begin(),name.end());h.push_back(0);}
    h.push_back(0);
    std::ofstream f(root/"SOUND.HD3",std::ios::binary); f.write(reinterpret_cast<const char*>(h.data()),h.size());
}
int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1], "local-pal") == 0) {
        fs::path root(argv[2]);
        std::string error;
        sound_stream_data::Index index;
        assert(index.LoadIndex(root, error));
        std::ifstream f(root / "SOUND.HD3", std::ios::binary | std::ios::ate);
        Bytes bytes(static_cast<size_t>(f.tellg())); f.seekg(0);
        f.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        auto word = [&](size_t at) {
            return unsigned(bytes[at]) | (unsigned(bytes[at+1]) << 8) |
                   (unsigned(bytes[at+2]) << 16) | (unsigned(bytes[at+3]) << 24);
        };
        unsigned records = word(0) / 16;
        assert(records == 3102);
        std::vector<int16_t> samples;
        uint32_t rate; uint16_t channels;
        uint64_t sample_count = 0;
        for (unsigned i = 0; i + 1 < records; ++i) {
            std::string name(reinterpret_cast<char *>(bytes.data() + word(i * 16)));
            if (!index.ReadPcm(name, samples, rate, channels, error)) {
                std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str()); return 1;
            }
            assert(rate == 48000 && channels == 1 && !samples.empty());
            sample_count += samples.size();
        }
        std::printf("local PAL SOUND: %u indexed names decoded, %llu PCM samples\n", records - 1,
                    static_cast<unsigned long long>(sample_count));
        return 0;
    }
    const auto root=fs::temp_directory_path()/"sound_stream_data_standalone"; fs::remove_all(root); fs::create_directories(root);
    const auto wave=Wave(); const unsigned sector0=0, sector1=1;
    Bytes dat(2048+wave.size(),0); std::copy(wave.begin(),wave.end(),dat.begin()); std::copy(wave.begin(),wave.end(),dat.begin()+2048);
    dat[2048 + wave.size() - 4] = 0x77; // Distinct duplicate proves first-match selection.
    {std::ofstream f(root/"SOUND.DAT",std::ios::binary);f.write(reinterpret_cast<const char*>(dat.data()),dat.size());}
    std::string err; sound_stream_data::Index index;
    WriteIndex(root,{"8640040.wav","8640040.wav"},{sector0,sector1},{static_cast<unsigned>(wave.size()),static_cast<unsigned>(wave.size())});
    assert(index.LoadIndex(root,err)); std::vector<std::int16_t> samples; std::uint32_t rate=0; std::uint16_t channels=0;
    assert(index.ReadPcm("8640040.WAV",samples,rate,channels,err)); assert(rate==48000&&channels==1&&samples.size()==2&&samples[0]==0x1234&&samples[1]==-2);
    assert(!index.ReadPcm("missing.wav",samples,rate,channels,err)&&!err.empty());
    // The upstream extractor keeps streams outside the DATA-only file index.
    fs::create_directories(root/"extracted"/"sound");
    fs::copy_file(root/"SOUND.HD3",root/"extracted"/"sound"/"sound.hd3");
    fs::copy_file(root/"SOUND.DAT",root/"extracted"/"sound"/"sound.dat");
    sound_stream_data::Index extracted;
    assert(extracted.LoadIndex(root/"extracted",err));
    assert(extracted.ReadPcm("8640040.wav",samples,rate,channels,err));
    assert(rate==48000&&channels==1&&samples.size()==2&&samples[0]==0x1234&&samples[1]==-2);
    // A corrupted record range is rejected before the record buffer is allocated.
    WriteIndex(root,{"broken.wav"},{99},{static_cast<unsigned>(wave.size())}); assert(index.LoadIndex(root,err));
    assert(!index.ReadPcm("broken.wav",samples,rate,channels,err)&&!err.empty());
    // A malformed chunk length inside a valid indexed record is rejected.
    Patch32(dat,16,0xffffffffu); {std::ofstream f(root/"SOUND.DAT",std::ios::binary);f.write(reinterpret_cast<const char*>(dat.data()),dat.size());}
    WriteIndex(root,{"bad.wav"},{0},{static_cast<unsigned>(wave.size())}); assert(index.LoadIndex(root,err));
    assert(!index.ReadPcm("bad.wav",samples,rate,channels,err)&&!err.empty());
    fs::remove_all(root); return 0;
}
