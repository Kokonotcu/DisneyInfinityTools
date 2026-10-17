// octtest's modes, by the file that holds them; octtest.cpp's table maps each flag to one. Main thread.
#pragma once
#include "run.hpp"

namespace octtest {

// ---- acceptance tests: they judge the result, and fail the exit code ----

// test_oct.cpp
int testRoundTrip(const Args& a);
int testCounts(const Args& a);
int testVtxf(const Args& a);

// test_data.cpp
int testKeep(const Args& a);
int testChd(const Args& a);
int testDct(const Args& a);
int testMissionChain(const Args& a);
int testGpScan(const Args& a);

// test_cho.cpp
int testChoLayout(const Args& a);
int testChoTime(const Args& a);
int testChoTime10(const Args& a);
int testChoAnim(const Args& a);
int testChoScene(const Args& a);

// test_choconst.cpp
int testChoConst(const Args& a);
int testVariants(const Args& a);

// test_clips.cpp
int testChoClips(const Args& a);
int testClipIds(const Args& a);
int testChoFile(const Args& a);
int testChoCorpus(const Args& a);
int testBanm(const Args& a);

// test_audio.cpp
int testFmod(const Args& a);
int testVo(const Args& a);
int testHost(const Args& a);

// test_system.cpp
int testReadable(const Args& a);
int testUi(const Args& a);

// test_render.cpp
int testGen30(const Args& a);
int testWarm(const Args& a);
int testArc(const Args& a);
int testVfxChan(const Args& a);

// ---- converters: they write or print a transform's output for an oracle or the golden files ----

// convert_data.cpp
int convertGroups30(const Args& a);
int convertCurrency10(const Args& a);
int convertSxKeys(const Args& a);
int convertAi(const Args& a);
int convertSx(const Args& a);
int convertChest(const Args& a);
int convertPerc(const Args& a);
int convertDataMap(const Args& a);
int convertDbTriggers(const Args& a);
int convertChoAlias(const Args& a);
int convertDmEval(const Args& a);
int convertPlayset(const Args& a);
int convertAvPerc(const Args& a);
int convertGameDb(const Args& a);
int convertPsDna(const Args& a);

// convert_graphs.cpp
int convertOct(const Args& a);
int convertByPlugin(const Args& a);
int convertMer(const Args& a);
int convertActivity(const Args& a);
int convertChallenge(const Args& a);
int convertBeaconMap(const Args& a);
int convertZones10(const Args& a);
int convertMenu(const Args& a);
int convertSurf(const Args& a);
int convertTools(const Args& a);
int convertNav(const Args& a);
int convertSkin(const Args& a);
int convertBtd(const Args& a);
int convertBent(const Args& a);
int convertTexName(const Args& a);
int convertClipDump(const Args& a);

// convert_render.cpp
int convertRender(const Args& a);
int convertMtb(const Args& a);
int convertPd(const Args& a);

// convert_audio.cpp
int convertMixer(const Args& a);
int convertFevDeps(const Args& a);

// ---- probes: they show one input on request ----

// probe.cpp
int probeZip(const Args& a);
int probeMember(const Args& a);
int probeRead10(const Args& a);
int probeVoPlay(const Args& a);

}  // namespace octtest
