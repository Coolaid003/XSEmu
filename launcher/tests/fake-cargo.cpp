// cargo as VIctory (DexNom)'s build runs it, for tst_runnerstate on every OS: --target puts the bins
// under <CARGO_TARGET_DIR>/<triple>/release and the host build scripts under <CARGO_TARGET_DIR>/release.
// The first run fails like a broken build does; it leaves "attempted" beside this program, and the
// next run succeeds.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    bool target = false;
    for (int i = 1; i + 1 < argc; ++i)
        target = target || (std::string(argv[i]) == "--target" && std::string(argv[i + 1]) == "x86_64-pc-windows-msvc");
    if (!target) {
        std::puts("error: no --target");
        return 2;
    }
    const char* dir = std::getenv("CARGO_TARGET_DIR");
    if (!dir || !*dir) {
        std::puts("error: CARGO_TARGET_DIR is not set");
        return 2;
    }
    const fs::path t = dir;
    for (const char* sub : {"build", "deps", "examples", "incremental"})
        fs::create_directories(t / "release" / sub);
    std::puts("   Compiling victory-formats v0.1.0");
    const fs::path attempted = fs::absolute(argv[0]).parent_path() / "attempted";
    if (!fs::exists(attempted)) {
        std::ofstream(attempted).put('\n');
        std::puts("error[E0425]: cannot find value `BCPACK` in this scope");
        std::puts("error: could not compile `victory-formats` (lib) due to 1 previous error");
        std::fflush(stdout);
        return 101;
    }
    const fs::path bins = t / "x86_64-pc-windows-msvc" / "release";
    fs::create_directories(bins);
    for (const char* b : {"victory", "victory-play", "victory-warm"})
        std::ofstream(bins / (std::string(b) + ".exe")) << "MZ " << b << '\n';
    std::puts("    Finished `release` profile [optimized] target(s) in 61.02s");
    return 0;
}
