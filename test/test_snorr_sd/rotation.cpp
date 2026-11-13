#define SNORR_SD_GATEWAY 1
#define SNORR_RECORDER_HOST_TEST 1
#include "../../src/mesh/SnorrRecorder.cpp"
#include <cassert>

int main()
{
    using namespace recorder;
    source=0x12345678;boot=7;
    bootEntry.kind=Boot; bootEntry.seq=1;
    bootEntry.utc=1609459200; // January 2021. The next boot is six months later.
    fs::SDFS::available=false;
    assert(!mount());
    fs::SDFS::available=true;
    assert(mount());
    std::string oldest=path;
    assert(closeSegment());
    unmount();
    bootEntry.utc+=182*86400;
    ++boot;
    std::string foreign=std::string(folder)+"/FFFFFFFF.SML";
    fixture::files[foreign]={1,2,3,4};
    fixture::files["0:/notes.txt"]={1,2};
    assert(mount());
    std::string next=path;
    assert(oldest!=next && fixture::files.count(oldest));
    assert(closeSegment());
    // An unclosed, corrupted owned segment and a foreign name must both survive.
    fixture::files[std::string(folder)+"/00000003.SML"]={4,5};
    Entry e{};e.kind=RawRx;e.length=250;
    assert(append(e));
    assert(fixture::files.count(oldest) && fixture::files.count(next));
    fixture::clusters=fixture::allocated();
    for(int n=0;n<3 && !evictions;++n) assert(append(e));
    assert(evictions==1 && evicted==1);
    assert(!fixture::files.count(oldest) && fixture::files.count(next));
    assert(fixture::files.count(foreign) && fixture::files.count("0:/notes.txt"));
    fixture::writeFail=true;
    assert(!append(e));
    unmount();
    fixture::writeFail=false;
    fixture::clusters+=8;
    assert(mount());
    assert(closedSegment(next.c_str(),2));
}
