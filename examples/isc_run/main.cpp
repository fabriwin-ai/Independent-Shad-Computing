// isc_run - compile a .fa pipeline and run it over synthetic or PPM frames.
//
//   isc_run <pipeline.fa> [options]
//     --backend auto|cpu|vulkan   override render.backend
//     --profile NAME              override with a built-in profile
//     --frames N                  frames to process (default 1)
//     --size WxH                  synthetic frame size (default 1280x720)
//     --input FILE.ppm            use a PPM instead of the synthetic pattern
//     --out FILE.ppm              write the last enhanced frame
//     --lod 0..3 --scale S        pin a rung (disables adaptation for frame 1)
//     --host-ms MS                simulated host frame time fed to the controller
//     --threads N                 scheduler threads
//     --validation                enable Vulkan validation layers
//     --compare                   also run the CPU reference and report max diff
//     --dump-ast --dump-ir        print the front-end results

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "isc/isc.hpp"

namespace {

void usage() {
    std::printf(
        "usage: isc_run <pipeline.fa> [--backend auto|cpu|vulkan] [--profile NAME] [--frames N]\n"
        "               [--size WxH] [--input in.ppm] [--out out.ppm] [--lod 0..3 --scale S]\n"
        "               [--host-ms MS] [--threads N] [--validation] [--compare] [--dump-ast] [--dump-ir]\n");
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    std::string fa = argv[1], input_path, out_path, profile;
    isc::BackendKind backend = isc::BackendKind::Auto;
    int frames = 1, lod = -1, threads = 0;
    float scale = 1.0f;
    double host_ms = 0.0;
    std::uint32_t W = 1280, H = 720;
    bool validation = false, compare = false, dump_ast = false, dump_ir = false;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--backend") {
            if (!isc::parse_backend(next(), backend)) {
                std::fprintf(stderr, "unknown backend\n");
                return 2;
            }
        } else if (a == "--profile") profile = next();
        else if (a == "--frames") frames = std::atoi(next());
        else if (a == "--size") {
            if (std::sscanf(next(), "%ux%u", &W, &H) != 2) {
                std::fprintf(stderr, "--size expects WxH\n");
                return 2;
            }
        } else if (a == "--input") input_path = next();
        else if (a == "--out") out_path = next();
        else if (a == "--lod") lod = std::atoi(next());
        else if (a == "--scale") scale = static_cast<float>(std::atof(next()));
        else if (a == "--host-ms") host_ms = std::atof(next());
        else if (a == "--threads") threads = std::atoi(next());
        else if (a == "--validation") validation = true;
        else if (a == "--compare") compare = true;
        else if (a == "--dump-ast") dump_ast = true;
        else if (a == "--dump-ir") dump_ir = true;
        else {
            usage();
            return 2;
        }
    }

    std::string source;
    if (!read_file(fa, source)) {
        std::fprintf(stderr, "cannot read %s\n", fa.c_str());
        return 1;
    }
    if (dump_ast) {
        auto parsed = isc::lang::parse(source);
        std::printf("%s", isc::lang::dump(parsed.program).c_str());
    }

    isc::RuntimeOptions opts;
    opts.backend = backend;
    opts.threads = static_cast<std::uint32_t>(threads);
    opts.vulkan_validation = validation;
    opts.profile_override = profile;
    isc::Runtime rt(opts);
    std::string report;
    const bool ok = rt.load(source, fa, &report);
    std::printf("%s", report.c_str());
    if (!ok) return 1;
    if (dump_ir) std::printf("%s", isc::ir::dump(rt.module()).c_str());
    std::printf("%s", isc::describe(rt.profile()).c_str());

    isc::RenderTarget in, out;
    if (!input_path.empty()) {
        std::string err;
        if (!isc::util::read_ppm(input_path, in, &err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
    } else {
        in.resize(W, H);
    }

    if (lod >= 0 && !rt.controller().force(static_cast<isc::Lod>(lod), scale)) {
        std::fprintf(stderr, "rung LOD%d @ %.3f is not on this profile's ladder\n", lod, scale);
        return 1;
    }

    isc::FrameStats st;
    double total_ms = 0.0, total_gpu = 0.0;
    for (int f = 0; f < frames; ++f) {
        if (input_path.empty()) isc::util::fill_test_pattern(in, static_cast<std::uint32_t>(f));
        std::string err;
        if (!rt.process(in, out, host_ms, &st, &err)) {
            std::fprintf(stderr, "frame %d failed: %s\n", f, err.c_str());
            return 1;
        }
        total_ms += st.layer_ms;
        total_gpu += st.gpu_ms;
        if (f == 0 || st.rung_changed || f == frames - 1) {
            std::printf("frame %4llu  %-15s scale %.3f  work %ux%u  layer %.3f ms  gpu %.3f ms  load %.2f%s\n",
                        static_cast<unsigned long long>(st.frame), std::string(isc::to_string(st.lod)).c_str(),
                        st.resolution_scale, st.work_width, st.work_height, st.layer_ms, st.gpu_ms, st.load,
                        st.rung_changed ? "  -> rung changed" : "");
            for (const auto& s : st.stages)
                std::printf("             stage %-10s weight %.4f -> %.4f  iterations %u  loss %.3g -> %.3g\n",
                            s.name.c_str(), s.weight_in, s.weight_out, s.iterations, s.loss_initial, s.loss_final);
        }
    }
    std::printf("avg layer %.3f ms, avg gpu %.3f ms over %d frames\n", total_ms / frames, total_gpu / frames, frames);
    std::printf("memory: used %.1f MiB, reserved %.1f MiB / budget %.1f MiB, blocks %u, cache %.1f MiB "
                "(hits %llu, misses %llu, evictions %llu)\n",
                st.memory.used_bytes / 1048576.0, st.memory.reserved_bytes / 1048576.0,
                st.memory.budget_bytes / 1048576.0, st.memory.blocks, st.memory.cache_bytes / 1048576.0,
                static_cast<unsigned long long>(st.memory.cache_hits),
                static_cast<unsigned long long>(st.memory.cache_misses),
                static_cast<unsigned long long>(st.memory.cache_evictions));

    if (compare) {
        isc::RuntimeOptions co = opts;
        co.backend = isc::BackendKind::Cpu;
        isc::Runtime ref(co);
        if (ref.load(source, fa)) {
            ref.controller().force(st.lod, st.resolution_scale);
            isc::RenderTarget ref_out;
            isc::FrameStats rs;
            ref.process(in, ref_out, 0.0, &rs);
            std::printf("compare vs CPU reference: max abs diff %.6f, mean abs diff %.8f\n",
                        isc::max_abs_diff(out, ref_out), isc::mean_abs_diff(out, ref_out));
        }
    }

    if (!out_path.empty()) {
        std::string err;
        if (!isc::util::write_ppm(out_path, out, &err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        std::printf("wrote %s\n", out_path.c_str());
    }
    return 0;
}
