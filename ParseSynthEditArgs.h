#pragma once
#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// One CLI operation. Parsed from argv (or a script line) and executed in
// argv-order against a single in-process SynthEditApp.
struct SynthEditCommand
{
    std::string verb;                    // "load", "screenshot", "add-module", ...
    std::vector<std::string> args;       // verb-specific positional/named args

    // Optional alias bound to the handle this command emits. Only meaningful
    // for handle-emitting verbs (add-module, containerise). When set, the
    // dispatcher records aliases["$" + as] = <emitted handle> on success, so
    // later verbs can refer to "$<as>" in place of a literal handle.
    std::string as;
};

// Init-time settings + ordered command list produced by ParseSynthEditArgs.
struct SynthEditConfig
{
    // Init-time: applied before SynthEditApp::InitInstance().
    bool quiet = false;
    bool rescanModules = false;
    bool rescanIncludesVsts = false;
    std::string overrideSamplesFolder;
    std::string overrideFactorySemsFolder;
    std::string rego;
    std::string regoname;
    std::string autosavePresetsFolder;   // consumed by export-plugin verb

    // -renderer <cpu|d2d>: pick the drawing backend BEFORE the first render
    // creates the process-lifetime screenshot factory. "cpu" = gmpi_ui's
    // software renderer (needs SE_SOFTWARE_RENDERER_OPTION compiled in);
    // "d2d"/"native" = the platform's native backend (the default). Empty =
    // leave the platform default alone. Used by --profile-scroll to compare
    // the two renderers on identical workloads.
    std::string renderer;

    // When true, main emits one JSON object per command to stdout
    // (banner + chatter go to stderr instead). Auto-enabled by --script
    // and any of the new manipulation verbs (add-module, dump, etc.) so
    // existing flag combinations preserve their classic stdout output.
    bool jsonOutput = false;

    // Executed in order after init. Each existing flag (positional load,
    // --screenshot, --autosavevst, --autorender, --upgrade) becomes one
    // command; new flags (--add-module, --connect, --dump, --script, ...)
    // append additional commands.
    std::vector<SynthEditCommand> commands;

    // Backwards-compat scalar mirrors of common commands. Populated by
    // ParseSynthEditArgs after argv parsing finishes. SynthEditCL dispatches
    // off `commands` in order; other callers (MainWindow.xaml.cpp) read
    // these scalars and ignore the command list, which is fine because
    // those callers only care about the legacy single-action workflow.
    std::string loadFile;       // path of the load command, or empty
    int         autoSaveType{}; // 0 = none, 3 = vst[3], 4 = juce, 5 = upgrade-and-save, 6 = gmpi-only
    bool        autoRender{};   // true if --autorender is present
};

namespace synthedit_args_detail
{
    // Flags are hyphen-introduced on every platform: -flag and --flag. We used
    // to also accept /flag on Windows, but that diverged from the Mac/Linux
    // builds and collided with forward-slash file paths, so flags are now
    // hyphen-only everywhere.
    inline bool isFlag(std::string_view s)
    {
        return s.size() >= 2 && s[0] == '-';
    }

    // "-160,100" is a position, not a flag, but it is hyphen-introduced and so
    // isFlag() says otherwise - which used to cost --add-module its coordinates
    // whenever they were negative, silently placing the module at the origin
    // and then reporting the pair as an unrecognised verb. Flag names are
    // alphabetic, so a token shaped like a number pair can never collide with
    // one.
    inline bool isCoordinatePair(std::string_view s)
    {
        const auto comma = s.find(',');
        if (comma == std::string_view::npos || comma == 0 || comma + 1 == s.size())
            return false;

        auto isNumber = [](std::string_view t)
        {
            if (!t.empty() && (t.front() == '-' || t.front() == '+'))
                t.remove_prefix(1);
            if (t.empty())
                return false;

            bool seenDot = false;
            for (const char c : t)
            {
                if (c == '.')
                {
                    if (seenDot)
                        return false;
                    seenDot = true;
                }
                else if (c < '0' || c > '9')
                    return false;
            }
            return true;
        };

        return isNumber(s.substr(0, comma)) && isNumber(s.substr(comma + 1));
    }

    // Every flag name the parser below recognises. Used only to classify an
    // unmatched flag (typo vs. known verb missing its operands) and to offer
    // near-matches in the error — parsing itself is still done by the
    // if-chain, so a name missing from this list costs a worse message, not
    // a behaviour change.
    inline const std::vector<std::string_view>& knownFlags()
    {
        static const std::vector<std::string_view> names = {
            "quiet", "rescan", "rescanvsts", "samplesfolder", "rego", "regoname",
            "factorysemsfolder", "autosavepresets", "screenshot", "autosavevst",
            "autosavevst3", "autosavejuce", "autosavegmpi", "upgrade", "autorender",
            "new", "load", "save-as", "dump", "list-modules", "add-module", "connect",
            "patch-cable", "select", "deselect-all", "set-pin", "render-audio",
            "delete", "pointer-down", "pointer-move", "pointer-up", "hover", "drag",
            "move", "key", "type", "get-param", "set-param", "containerise", "as", "script",
            "ping", "rename", "start-audio", "stop-audio", "audio-state",
            "renderer", "profile-scroll", "set-plugin-info",
        };
        return names;
    }

    // Retired flags that legacy build scripts still pass. Deliberately ignored
    // (the screenshot verb takes these as sub-args now); listing them keeps the
    // unknown-flag error below from firing on invocations that used to work.
    inline bool isRetiredFlag(std::string_view flag)
    {
        return flag == "screenshotview" || flag == "screenshotscale"
            || flag == "whitebackground";
    }

    // Levenshtein distance, capped work — the inputs are short flag names.
    inline size_t flagDistance(std::string_view a, std::string_view b)
    {
        std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
        for (size_t i = 1; i <= a.size(); ++i)
        {
            cur[0] = i;
            for (size_t j = 1; j <= b.size(); ++j)
                cur[j] = std::min<size_t>(prev[j] + 1,
                    std::min<size_t>(cur[j - 1] + 1,
                        prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)));
            prev.swap(cur);
        }
        return prev[b.size()];
    }

    // Up to three known flags closest to `flag`, comma-separated ("" if none
    // are close). Substring hits count as near-matches so "--modules" suggests
    // "list-modules"/"add-module".
    inline std::string nearestFlags(std::string_view flag)
    {
        const size_t threshold = std::max<size_t>(2, flag.size() / 3);
        std::vector<std::pair<size_t, std::string_view>> hits;
        for (auto name : knownFlags())
        {
            size_t d = flagDistance(flag, name);
            if (name.find(flag) != std::string_view::npos ||
                flag.find(name) != std::string_view::npos)
                d = std::min<size_t>(d, 1);
            if (d <= threshold)
                hits.emplace_back(d, name);
        }
        std::stable_sort(hits.begin(), hits.end(),
                         [](const auto& l, const auto& r) { return l.first < r.first; });
        std::string out;
        for (size_t i = 0; i < hits.size() && i < 3; ++i)
        {
            if (!out.empty()) out += ", ";
            out += "--";
            out += hits[i].second;
        }
        return out;
    }
}

// Strip leading '-' from a flag token so the bare name remains (handles both
// -flag and --flag).
inline std::string_view stripFlagDashes(std::string_view s)
{
    while (!s.empty() && s.front() == '-')
        s.remove_prefix(1);
    return s;
}

// Parses argv into a SynthEditConfig. New verb flags map to ordered commands.
//
// Screenshot options are *sub-args* of the screenshot verb itself:
//   --screenshot <path> [--view panel|structure] [--scale N] [--white]
// Sub-args are greedily consumed until the next non-screenshot flag. The old
// staged flags (--screenshotview / --whitebackground / --screenshotscale)
// are gone; legacy callers using them will get screenshots at default options
// instead of an error.
inline SynthEditConfig ParseSynthEditArgs(std::vector<std::string_view>& args)
{
    using synthedit_args_detail::isFlag;
    using synthedit_args_detail::isCoordinatePair;

    SynthEditConfig cfg;

    auto pushCmd = [&](std::string verb, std::vector<std::string> a = {}) {
        cfg.commands.push_back({std::move(verb), std::move(a)});
    };

    // The positional file argument is conventionally an input to the
    // whole command line, not an action scheduled at its argv position.
    // We stash it here and prepend a "load" command at the front before
    // returning, so `--screenshot foo.png project.synthedit` still loads
    // first as users expect.
    std::string positionalLoad;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args[i];

        if (!isFlag(arg))
        {
            // A bare (non-flag) token is taken as the positional project path.
            // Project files always carry an extension (.synthedit /
            // .syntheditprefab), so a '.' (or a Windows backslash) is enough to
            // recognise one. We deliberately DON'T treat a leading '/' as a path
            // signal: now that '/' is no longer a flag char, a stray legacy
            // '/switch' would otherwise be silently misread as a filename.
            // Genuine POSIX paths still match via the extension's '.'.
            const bool looksLikePath =
                arg.find('\\') != std::string_view::npos ||
                arg.find('.')  != std::string_view::npos;
            if (looksLikePath && positionalLoad.empty())
                positionalLoad = std::string(arg);
            continue;
        }

        // Accept both -flag and --flag (and /flag on Windows). Strip all
        // leading '-' and '/' so the comparisons below see the bare name.
        std::string_view flag = stripFlagDashes(arg);

        // ---- session-level settings (no command emitted) ----
        if (flag == "quiet")     { cfg.quiet = true; continue; }
        if (flag == "rescan")    { cfg.rescanModules = true; continue; }
        if (flag == "rescanvsts"){ cfg.rescanIncludesVsts = true; continue; }

        if (i + 1 < args.size())
        {
            if (flag == "samplesfolder")     { cfg.overrideSamplesFolder     = std::string(args[++i]); continue; }
            if (flag == "rego")              { cfg.rego                      = std::string(args[++i]); continue; }
            if (flag == "regoname")          { cfg.regoname                  = std::string(args[++i]); continue; }
            if (flag == "factorysemsfolder") { cfg.overrideFactorySemsFolder = std::string(args[++i]); continue; }
            if (flag == "autosavepresets")   { cfg.autosavePresetsFolder     = std::string(args[++i]); continue; }
            if (flag == "renderer")          { cfg.renderer                  = std::string(args[++i]); continue; }
        }

        if (flag == "screenshot" && i + 1 < args.size())
        {
            const std::string path = std::string(args[++i]);
            std::string view = "structure";
            bool        white = false;
            float       scale = 1.0f;
            bool        includeBrowser    = false;
            bool        includeProperties = false;
            std::string containerHandle;   // empty = render the master container

            // Greedily consume sub-args (--view / --scale / --white /
            // --include-browser / --include-properties / --container) until
            // the next non-sub-arg. Anything else terminates and is left for
            // the outer loop to interpret.
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);

                if (subFlag == "view" && i + 2 < args.size())
                {
                    i += 2;
                    view = std::string(args[i]);
                }
                else if (subFlag == "scale" && i + 2 < args.size())
                {
                    i += 2;
                    try { scale = std::stof(std::string(args[i])); }
                    catch (...) { scale = 1.0f; }
                    scale = std::clamp(scale, 0.1f, 16.0f);
                }
                else if (subFlag == "transparent" || subFlag == "white")
                {
                    // --white is the legacy spelling; both clear the canvas
                    // to alpha-0 (so the PNG composites onto any backdrop).
                    i += 1;
                    white = true;
                }
                else if (subFlag == "include-browser")
                {
                    // Compose the Module Browser onto the left of the
                    // editor view, mirroring the GUI app's full-window
                    // layout but in a single offscreen bitmap.
                    i += 1;
                    includeBrowser = true;
                }
                else if (subFlag == "include-properties")
                {
                    // Compose the Properties Browser onto the right of the
                    // editor view. Reflects the first selected module in
                    // the master container; --select <handle> beforehand
                    // picks which one. Composes with --include-browser
                    // for the full GUI three-pane layout.
                    i += 1;
                    includeProperties = true;
                }
                else if (subFlag == "container" && i + 2 < args.size())
                {
                    // Render a nested sub-container's panel/structure instead
                    // of the master. The handle is one reported by --dump.
                    // Lets you screenshot a plugin's GUI when the controls
                    // live inside a child container (Controls-on-Parent off).
                    i += 2;
                    containerHandle = std::string(args[i]);
                }
                else
                {
                    break;
                }
            }

            pushCmd("screenshot", {
                path, view, white ? "1" : "0", std::to_string(scale),
                includeBrowser ? "1" : "0",
                includeProperties ? "1" : "0",
                containerHandle,
            });
            // Screenshot is one of the structured-output verbs: emit JSONL so
            // MCP/script callers get a parseable {"cmd":"screenshot",...} line
            // (and banner/chatter divert to stderr). Legacy plain-text tools
            // that only check exit code + PNG existence are unaffected.
            cfg.quiet = true;
            cfg.jsonOutput = true;
            continue;
        }
        // --profile-scroll: render the loaded project's structure (or panel)
        // view N times into a fixed-size offscreen viewport while sweeping the
        // view centre across the content — the same full-viewport redraw a
        // user's wheel/pan scroll produces — and report per-frame timing
        // stats. Combine with the -renderer global flag to compare backends.
        if (flag == "profile-scroll")
        {
            std::string view     = "structure";
            std::string frames   = "300";
            std::string seconds  = "0";
            std::string width    = "1600";
            std::string height   = "1000";
            std::string zoom     = "1";
            std::string warmup   = "16";
            std::string csv;
            std::string png;
            std::string coverage;   // empty = the verb's default (2/3)
            std::string sample;     // path for a self-time profile report

            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);

                if      (subFlag == "view"     && i + 2 < args.size()) { i += 2; view     = std::string(args[i]); }
                else if (subFlag == "frames"   && i + 2 < args.size()) { i += 2; frames   = std::string(args[i]); }
                else if (subFlag == "seconds"  && i + 2 < args.size()) { i += 2; seconds  = std::string(args[i]); }
                else if (subFlag == "width"    && i + 2 < args.size()) { i += 2; width    = std::string(args[i]); }
                else if (subFlag == "height"   && i + 2 < args.size()) { i += 2; height   = std::string(args[i]); }
                else if (subFlag == "zoom"     && i + 2 < args.size()) { i += 2; zoom     = std::string(args[i]); }
                else if (subFlag == "warmup"   && i + 2 < args.size()) { i += 2; warmup   = std::string(args[i]); }
                else if (subFlag == "csv"      && i + 2 < args.size()) { i += 2; csv      = std::string(args[i]); }
                else if (subFlag == "png"      && i + 2 < args.size()) { i += 2; png      = std::string(args[i]); }
                else if (subFlag == "coverage" && i + 2 < args.size()) { i += 2; coverage = std::string(args[i]); }
                else if (subFlag == "sample"   && i + 2 < args.size()) { i += 2; sample   = std::string(args[i]); }
                else break;
            }

            pushCmd("profile-scroll",
                    {std::move(view), std::move(frames), std::move(seconds),
                     std::move(width), std::move(height), std::move(zoom),
                     std::move(warmup), std::move(csv), std::move(png),
                     std::move(coverage), std::move(sample)});
            cfg.quiet = true;
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "autosavevst" || flag == "autosavevst3")
        {
            pushCmd("export-plugin", {"3"});
            cfg.quiet = true;
            continue;
        }
        if (flag == "autosavejuce")
        {
            pushCmd("export-plugin", {"4"});
            cfg.quiet = true;
            continue;
        }
        if (flag == "upgrade")
        {
            pushCmd("export-plugin", {"5"});
            continue;
        }
        if (flag == "autosavegmpi")
        {
            pushCmd("export-plugin", {"6"});
            cfg.quiet = true;
            continue;
        }
        if (flag == "autorender")
        {
            std::string rate;
            // Optional sub-arg: --rate <Hz>. Greedy-consumed if it follows.
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);
                if (subFlag == "rate" && i + 2 < args.size())
                {
                    i += 2;
                    rate = std::string(args[i]);
                }
                else
                {
                    break;
                }
            }
            pushCmd("autorender", {std::move(rate)});
            continue;
        }

        // ---- new verbs (auto-enable JSONL output) ----
        if (flag == "new")
        {
            pushCmd("new");
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "save-as" && i + 1 < args.size())
        {
            pushCmd("save-as", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "dump" && i + 1 < args.size())
        {
            pushCmd("dump", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "list-modules" && i + 1 < args.size())
        {
            pushCmd("list-modules", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "add-module" && i + 1 < args.size())
        {
            std::string name = std::string(args[++i]);
            std::string xy = "0,0";
            // Accept "--add-module name --at x,y" or "--add-module name x,y".
            if (i + 1 < args.size())
            {
                std::string_view next = args[i + 1];
                if (next == "--at" || next == "-at" || next == "/at")
                {
                    if (i + 2 < args.size())
                    {
                        i += 2;
                        xy = std::string(args[i]);
                    }
                }
                else if (!isFlag(next) || isCoordinatePair(next))
                {
                    ++i;
                    xy = std::string(args[i]);
                }
            }
            pushCmd("add-module", {std::move(name), std::move(xy)});
            cfg.jsonOutput = true;
            continue;
        }
        // --set-plugin-info key=value [key=value ...]
        // Variable arity: consume every following token that isn't a flag, so
        // the whole identity can be set in one call. A value with spaces is
        // quoted by the script tokenizer, which keeps "name=My Synth" one token.
        if (flag == "set-plugin-info")
        {
            std::vector<std::string> settings;
            while (i + 1 < args.size() && !isFlag(args[i + 1]))
                settings.emplace_back(args[++i]);

            pushCmd("set-plugin-info", std::move(settings));
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "connect" && i + 2 < args.size())
        {
            std::string from = std::string(args[++i]);
            std::string to   = std::string(args[++i]);
            pushCmd("connect", {std::move(from), std::move(to)});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "patch-cable" && i + 2 < args.size())
        {
            std::string from = std::string(args[++i]);
            std::string to   = std::string(args[++i]);
            pushCmd("patch-cable", {std::move(from), std::move(to)});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "select" && i + 1 < args.size())
        {
            pushCmd("select", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "deselect-all")
        {
            pushCmd("deselect-all");
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "rename" && i + 2 < args.size())
        {
            std::string handle = std::string(args[++i]);
            std::string name   = std::string(args[++i]);
            pushCmd("rename", {std::move(handle), std::move(name)});
            cfg.jsonOutput = true;
            continue;
        }
        // Transport. Meaningful only in a host that owns an audio device; the
        // verbs say so themselves rather than pretending in the CLI.
        if (flag == "start-audio" || flag == "stop-audio" || flag == "audio-state")
        {
            pushCmd(std::string(flag));
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "set-pin" && i + 2 < args.size())
        {
            std::string target = std::string(args[++i]);
            std::string value  = std::string(args[++i]);
            pushCmd("set-pin", {std::move(target), std::move(value)});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "render-audio" && i + 1 < args.size())
        {
            std::string path = std::string(args[++i]);
            std::string from;       // Left / mono input
            std::string fromR;      // Right input (optional, makes the WAV stereo)
            std::string duration;
            std::string rate;
            // Greedy sub-args: --from / --from-r / --duration / --rate
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);
                if (subFlag == "from" && i + 2 < args.size())
                {
                    i += 2;
                    from = std::string(args[i]);
                }
                else if (subFlag == "from-r" && i + 2 < args.size())
                {
                    i += 2;
                    fromR = std::string(args[i]);
                }
                else if (subFlag == "duration" && i + 2 < args.size())
                {
                    i += 2;
                    duration = std::string(args[i]);
                }
                else if (subFlag == "rate" && i + 2 < args.size())
                {
                    i += 2;
                    rate = std::string(args[i]);
                }
                else
                {
                    break;
                }
            }
            pushCmd("render-audio",
                    {std::move(path), std::move(from), std::move(duration),
                     std::move(rate), std::move(fromR)});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "delete" && i + 1 < args.size())
        {
            pushCmd("delete", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }

        // ---- headless GUI input verbs (drive the live panel session) ----
        // All coordinates are panel-logical DIPs (same space as <panelRect>).
        // These are only meaningful within ONE process (a --script run or one
        // argv line), because the panel session state doesn't survive process
        // exit. See SynthEditCL/CLAUDE.md.
        //
        // --pointer-down / --pointer-move / --pointer-up <x,y> [--flags a,b,c]
        if ((flag == "pointer-down" || flag == "pointer-move" || flag == "pointer-up")
            && i + 1 < args.size())
        {
            std::string xy = std::string(args[++i]);
            std::string flags;
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);
                if (subFlag == "flags" && i + 2 < args.size()) { i += 2; flags = std::string(args[i]); }
                else break;
            }
            pushCmd(std::string(flag), {std::move(xy), std::move(flags)});
            cfg.jsonOutput = true;
            continue;
        }
        // --hover <x,y>   (clears hover when the point is off-content)
        if (flag == "hover" && i + 1 < args.size())
        {
            pushCmd("hover", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }
        // --move <handle> <x,y> [--view structure|panel]
        // A module has two independent positions — one in the structure view,
        // one on the panel — so the verb takes which one to move; structure is
        // the default. Distinct from --drag, which is a panel input GESTURE
        // (operating controls), not a model edit.
        if (flag == "move" && i + 2 < args.size())
        {
            std::string handle = std::string(args[++i]);
            std::string pos    = std::string(args[++i]);
            std::string view   = "structure";
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);
                if (subFlag == "view" && i + 2 < args.size()) { i += 2; view = std::string(args[i]); }
                else break;
            }
            pushCmd("move", {std::move(handle), std::move(pos), std::move(view)});
            cfg.jsonOutput = true;
            continue;
        }
        // --drag <x1,y1> <x2,y2> [--steps N] [--flags a,b,c]
        if (flag == "drag" && i + 2 < args.size())
        {
            std::string p1 = std::string(args[++i]);
            std::string p2 = std::string(args[++i]);
            std::string steps = "8";
            std::string flags;
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);
                if (subFlag == "steps" && i + 2 < args.size()) { i += 2; steps = std::string(args[i]); }
                else if (subFlag == "flags" && i + 2 < args.size()) { i += 2; flags = std::string(args[i]); }
                else break;
            }
            pushCmd("drag", {std::move(p1), std::move(p2), std::move(steps), std::move(flags)});
            cfg.jsonOutput = true;
            continue;
        }
        // --key <code> [--flags a,b,c]   (code: decimal or 0xNN; routes to the
        // active in-place key listener, e.g. NumberEdit)
        if (flag == "key" && i + 1 < args.size())
        {
            std::string code = std::string(args[++i]);
            std::string flags;
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);
                if (subFlag == "flags" && i + 2 < args.size()) { i += 2; flags = std::string(args[i]); }
                else break;
            }
            pushCmd("key", {std::move(code), std::move(flags)});
            cfg.jsonOutput = true;
            continue;
        }
        // --type "<text>"   (expands to a sequence of onKeyDowns)
        if (flag == "type" && i + 1 < args.size())
        {
            pushCmd("type", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }
        // --get-param <H>:<paramId> [--field value|normalized]
        if (flag == "get-param" && i + 1 < args.size())
        {
            std::string target = std::string(args[++i]);
            std::string field = "value";
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);
                if (subFlag == "field" && i + 2 < args.size()) { i += 2; field = std::string(args[i]); }
                else break;
            }
            pushCmd("get-param", {std::move(target), std::move(field)});
            cfg.jsonOutput = true;
            continue;
        }
        // --set-param <H>:<paramId> <value> [--field value|normalized]
        if (flag == "set-param" && i + 2 < args.size())
        {
            std::string target = std::string(args[++i]);
            std::string value  = std::string(args[++i]);
            std::string field  = "value";
            while (i + 1 < args.size() && isFlag(args[i + 1]))
            {
                std::string_view subFlag = stripFlagDashes(args[i + 1]);
                if (subFlag == "field" && i + 2 < args.size()) { i += 2; field = std::string(args[i]); }
                else break;
            }
            pushCmd("set-param", {std::move(target), std::move(value), std::move(field)});
            cfg.jsonOutput = true;
            continue;
        }
        if (flag == "containerise")
        {
            pushCmd("containerise");
            cfg.jsonOutput = true;
            continue;
        }
        // --as <name> attaches to the most recently pushed command (must be
        // a handle-emitting verb: add-module or containerise). The dispatcher
        // records the resulting handle as $<name> so later verbs can refer
        // to it without knowing the random RNG-generated handle value.
        if (flag == "as" && i + 1 < args.size() && !cfg.commands.empty())
        {
            cfg.commands.back().as = std::string(args[++i]);
            continue;
        }
        if (flag == "script" && i + 1 < args.size())
        {
            pushCmd("script", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }
        // --ping <token> echoes the token back as its own JSONL line. A host
        // that keeps a `--script -` process alive (the MCP session daemon)
        // appends one after each batch of verbs: the echo marks end-of-batch,
        // and any stream chatter before it belongs to that batch.
        if (flag == "ping" && i + 1 < args.size())
        {
            pushCmd("ping", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }

        // ---- explicit load (appears in argv order, unlike positional) ----
        // Explicit --load is the scripting/MCP entry point (bare positional
        // load stays plain-text for legacy build invocations), so emit JSONL
        // here too — otherwise an MCP se_load gets no parseable result line.
        if (flag == "load" && i + 1 < args.size())
        {
            pushCmd("load", {std::string(args[++i])});
            cfg.jsonOutput = true;
            continue;
        }

        // Nothing matched. Silently dropping the token here made every typo
        // look like "it ran and did nothing" to a caller scanning the JSONL,
        // so emit a command that fails loudly instead. Two distinct cases:
        //   - a known verb that fell through because its operands were missing
        //     (e.g. a trailing "--dump" with no path)
        //   - a genuinely unrecognised flag (typo) — offer near-matches
        if (!synthedit_args_detail::isRetiredFlag(flag))
        {
            const auto& known = synthedit_args_detail::knownFlags();
            const bool isKnownName = std::find(known.begin(), known.end(), flag) != known.end();
            pushCmd("unknown-verb", {
                std::string(arg),
                isKnownName ? std::string() : synthedit_args_detail::nearestFlags(flag),
                isKnownName ? "missing-args" : "unknown",
            });
        }
    }

    if (!positionalLoad.empty())
        cfg.commands.insert(cfg.commands.begin(),
            SynthEditCommand{"load", {std::move(positionalLoad)}});

    // Populate legacy scalar mirrors from the command list.
    for (const auto& c : cfg.commands)
    {
        if (c.verb == "load" && !c.args.empty() && cfg.loadFile.empty())
            cfg.loadFile = c.args[0];
        else if (c.verb == "export-plugin" && !c.args.empty() && cfg.autoSaveType == 0)
            try { cfg.autoSaveType = std::stoi(c.args[0]); } catch (...) {}
        else if (c.verb == "autorender")
            cfg.autoRender = true;
    }

    return cfg;
}
