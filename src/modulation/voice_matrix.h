/*
 * Shortcircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2024, Various authors, as described in the github
 * transaction log.
 *
 * ShortcircuitXT is released under the Gnu General Public Licence
 * V3 or later (GPL-3.0-or-later). The license is found in the file
 * "LICENSE" in the root of this repository or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Individual sections of code which comprises ShortcircuitXT in this
 * repository may also be used under an MIT license. Please see the
 * section  "Licensing" in "README.md" for details.
 *
 * ShortcircuitXT is inspired by, and shares code with, the
 * commercial product Shortcircuit 1 and 2, released by VemberTech
 * in the mid 2000s. The code for Shortcircuit 2 was opensourced in
 * 2020 at the outset of this project.
 *
 * All source for ShortcircuitXT is available at
 * https://github.com/surge-synthesizer/shortcircuit-xt
 */

#ifndef SCXT_SRC_MODULATION_VOICE_MATRIX_H
#define SCXT_SRC_MODULATION_VOICE_MATRIX_H

#include <string>
#include <array>
#include <vector>
#include "utils.h"
#include "dsp/processor/processor.h"

#include "mod_curves.h"
#include "sst/cpputils/constructors.h"

#include "sst/basic-blocks/mod-matrix/ModMatrix.h"

namespace scxt::engine
{
struct Zone;
struct Engine;
} // namespace scxt::engine
namespace scxt::voice
{
struct Voice;
}

namespace scxt::voice::modulation
{
struct MatrixConfig
{
    struct SourceIdentifier
    {
        uint32_t gid{0};
        uint32_t tid{0};
        uint32_t index{0};

        bool operator==(const SourceIdentifier &other) const
        {
            return gid == other.gid && tid == other.tid && index == other.index;
        }
    };

    using CurveIdentifier = scxt::modulation::ModulationCurves::CurveIdentifier;
    static std::function<float(float)> getCurveOperator(CurveIdentifier id)
    {
        return scxt::modulation::ModulationCurves::getCurveOperator(id);
    }

    struct TargetIdentifier
    {
        uint32_t gid{0}; // this has to be unique and streaming stable
        uint32_t tid{0};
        uint32_t index{0};

        bool operator==(const TargetIdentifier &other) const
        {
            return gid == other.gid && tid == other.tid && index == other.index;
        }
    };

    static constexpr bool IsFixedMatrix{true};
    static constexpr size_t FixedMatrixSize{12};

    using TI = TargetIdentifier;
    using SI = SourceIdentifier;

    struct RoutingExtraPayload
    {
        bool selConsistent{false};
        datamodel::pmd targetMetadata;
        float targetBaseValue; // only valid on UI thread
    };

    static std::string u2s(uint32_t u)
    {
        std::string res = "";
        res += (char)((u & 0xFF000000) >> 24);
        res += (char)((u & 0x00FF0000) >> 16);
        res += (char)((u & 0x0000FF00) >> 8);
        res += (char)((u & 0x000000FF) >> 0);
        return res;
    };

    template <typename T>
    static std::ostream &identifierToStream(std::ostream &os, const T &t, const char *nm)
    {
        os << nm << "[" << u2s(t.gid) << "/" << u2s(t.tid) << "/" << t.index
           << " #=" << std::hash<T>{}(t) << "]";
        return os;
    }
    template <typename T> static std::string identifierToString(const T &t, const char *nm)
    {
        std::ostringstream oss;
        identifierToStream(oss, t, nm);
        return oss.str();
    }

    template <typename T> static std::size_t identifierToHash(const T &t)
    {
        auto h1 = std::hash<uint32_t>{}((int)t.gid);
        auto h2 = std::hash<uint32_t>{}((int)t.tid);
        auto h3 = std::hash<uint32_t>{}((int)t.index);
        return h1 ^ (h2 << 2) ^ (h3 << 5);
    }

    static bool isTargetModMatrixDepth(const TargetIdentifier &t) { return t.gid == 'self'; }
    static size_t getTargetModMatrixElement(const TargetIdentifier &t)
    {
        assert(isTargetModMatrixDepth(t));
        return (size_t)t.index;
    }
};
} // namespace scxt::voice::modulation

template <> struct std::hash<scxt::voice::modulation::MatrixConfig::TargetIdentifier>
{
    std::size_t
    operator()(const scxt::voice::modulation::MatrixConfig::TargetIdentifier &s) const noexcept
    {
        return scxt::voice::modulation::MatrixConfig::identifierToHash(s);
    }
};

template <> struct std::hash<scxt::voice::modulation::MatrixConfig::SourceIdentifier>
{
    std::size_t
    operator()(const scxt::voice::modulation::MatrixConfig::SourceIdentifier &s) const noexcept
    {
        return scxt::voice::modulation::MatrixConfig::identifierToHash(s);
    }
};

inline std::ostream &operator<<(std::ostream &os,
                                const scxt::voice::modulation::MatrixConfig::TargetIdentifier &tg)
{
    scxt::voice::modulation::MatrixConfig::identifierToStream(os, tg, "Target");
    return os;
}

inline std::ostream &operator<<(std::ostream &os,
                                const scxt::voice::modulation::MatrixConfig::SourceIdentifier &tg)
{
    scxt::voice::modulation::MatrixConfig::identifierToStream(os, tg, "Source");
    return os;
}

namespace scxt::voice::modulation
{
struct Matrix : sst::basic_blocks::mod_matrix::FixedMatrix<MatrixConfig>
{
    bool forUIMode{false};
    std::unordered_map<MatrixConfig::TargetIdentifier, datamodel::pmd> activeTargetsToPMD;
    std::unordered_map<MatrixConfig::TargetIdentifier, float> activeTargetsToBaseValue;
};

struct MatrixEndpoints
{
    using TG = MatrixConfig::TargetIdentifier;
    using SR = MatrixConfig::SourceIdentifier;

    MatrixEndpoints(engine::Engine *e)
        : aeg(e, 0), eg2(e, 1),
          lfo{sst::cpputils::make_array_bind_last_index<LFOTarget, scxt::lfosPerZone>(e)},
          selfModulation(e),
          processorTarget{
              sst::cpputils::make_array_bind_last_index<ProcessorTarget,
                                                        scxt::processorsPerZoneAndGroup>(e)},
          mappingTarget(e), outputTarget(e), sources(e)
    {
    }

    // We will need to refactor this when we do group. One step at a time
    struct LFOTarget
    {
        uint32_t index{0};
        LFOTarget(engine::Engine *e, uint32_t p);
        TG rateT;
        const float *rateP{nullptr};
        struct Curve
        {
            Curve(uint32_t p)
                : deformT{'lfo ', 'cdfn', p}, attackT{'lfo ', 'catk', p}, delayT{'lfo ', 'cdel', p},
                  releaseT{'lfo ', 'crel', p}
            {
            }
            TG deformT, delayT, attackT, releaseT;
            const float *deformP{nullptr}, *delayP{nullptr}, *attackP{nullptr}, *releaseP{nullptr};
        } curve;
        struct Step
        {
            Step(uint32_t p) : smoothT{'lfo ', 'ssmt', p} {}
            TG smoothT;
            const float *smoothP{nullptr};
        } step;

        struct Env
        {
            Env(uint32_t p)
                : delayT{'lfo ', 'edly', p}, attackT{'lfo ', 'eatk', p}, holdT{'lfo ', 'ehld', p},
                  decayT{'lfo ', 'edcy', p}, sustainT{'lfo ', 'esus', p},
                  releaseT{'lfo ', 'erel', p}
            {
            }
            TG delayT, attackT, holdT, decayT, sustainT, releaseT;
            const float *delayP{nullptr}, *attackP{nullptr}, *holdP{nullptr}, *decayP{nullptr},
                *sustainP{nullptr}, *releaseP{nullptr};
        } env;

        void bind(Matrix &m, engine::Zone &z);
    };

    struct EGTarget
    {
        uint32_t index{0};
        EGTarget(engine::Engine *e, uint32_t p)
            : index(p), aT{'envg', 'atck', p}, hT{'envg', 'hld ', p}, dT{'envg', 'dcay', p},
              sT{'envg', 'sust', p}, rT{'envg', 'rels', p}, asT{'envg', 'atSH', p},
              dsT{'envg', 'dcSH', p}, rsT{'envg', 'rlSH', p}

        {
            std::string group = (index == 0 ? "AEG" : "EG2");
            registerVoiceModTarget(e, aT, group, "Attack");
            registerVoiceModTarget(e, hT, group, "Hold");
            registerVoiceModTarget(e, dT, group, "Decay");
            registerVoiceModTarget(e, sT, group, "Sustain");
            registerVoiceModTarget(e, rT, group, "Release");
            registerVoiceModTarget(e, asT, group, "Attack Shape");
            registerVoiceModTarget(e, dsT, group, "Decay Shape");
            registerVoiceModTarget(e, rsT, group, "Release Shape");
        }

        TG aT, hT, dT, sT, rT, asT, dsT, rsT;
        const float *aP{nullptr}, *hP{nullptr}, *dP{nullptr}, *sP{nullptr}, *rP{nullptr};
        const float *asP{nullptr}, *dsP{nullptr}, *rsP{nullptr};

        void bind(Matrix &m, engine::Zone &z);
    } aeg, eg2;

    std::array<LFOTarget, scxt::lfosPerZone> lfo;

    struct MappingTarget
    {
        MappingTarget(engine::Engine *e)
            : pitchOffsetT{'zmap', 'ptof', 0}, panT{'zmap', 'pan ', 0}, ampT{'zmap', 'ampl', 0},
              playbackRatioT{'zmap', 'pbrt', 0}
        {
            if (e)
            {
                registerVoiceModTarget(e, pitchOffsetT, "Mapping", "Pitch Offset");
                registerVoiceModTarget(e, panT, "Mapping", "Pan");
                registerVoiceModTarget(e, ampT, "Mapping", "Amplitude");
                registerVoiceModTarget(e, playbackRatioT, "Mapping", "Playback Ratio");
            }
        }
        TG pitchOffsetT, panT, ampT, playbackRatioT;

        const float *pitchOffsetP{nullptr}, *panP{nullptr}, *ampP{nullptr},
            *playbackRatioP{nullptr};

        void bind(Matrix &m, engine::Zone &z);

        float zeroBase{0.f}; // this is a base zero value for things which are not in the mod map
    } mappingTarget;

    struct OutputTarget
    {
        OutputTarget(engine::Engine *e) : panT{'zout', 'pan ', 0}, ampT{'zout', 'ampl', 0}
        {
            if (e)
            {
                registerVoiceModTarget(e, panT, "Output", "Pan");
                registerVoiceModTarget(e, ampT, "Output", "Amplitude");
            }
        }
        TG panT, ampT;

        const float *panP{nullptr}, *ampP{nullptr};

        void bind(Matrix &m, engine::Zone &z);
    } outputTarget;

    struct ProcessorTarget
    {
        uint32_t index;
        // This is out of line since it creates a caluclation using zone
        // innards and we can't have an include cycle
        ProcessorTarget(engine::Engine *e, uint32_t(p));

        TG mixT, fpT[scxt::maxProcessorFloatParams];
        const float *mixP{nullptr};
        const float *floatP[scxt::maxProcessorFloatParams]; // FIX CONSTANT
        float fp[scxt::maxProcessorFloatParams]{};

        void snapValues()
        {
            for (int i = 0; i < scxt::maxProcessorFloatParams; ++i)
                fp[i] = *floatP[i];
        }

        void bind(Matrix &m, engine::Zone &z);
    };
    std::array<ProcessorTarget, scxt::processorsPerZoneAndGroup> processorTarget;

    struct SelfModulationTarget
    {
        SelfModulationTarget(engine::Engine *e)
        {
            for (uint32_t i = 0; i < MatrixConfig::FixedMatrixSize; ++i)
            {
                selfT[i] = TG{'self', 'dpth', i};
                registerVoiceModTarget(e, selfT[i], "Mod Depth", "Row " + std::to_string(i + 1));
            }
        }
        std::array<TG, MatrixConfig::FixedMatrixSize> selfT;
    } selfModulation;

    struct Sources
    {
        Sources(engine::Engine *e)
            : lfoSources(e), midiSources(e), aegSource{'zneg', 'aeg ', 0},
              eg2Source{'zneg', 'eg2 ', 0}
        {
            registerVoiceModSource(e, aegSource, "", "AEG");
            registerVoiceModSource(e, eg2Source, "", "EG2");
        }
        struct LFOSources
        {
            LFOSources(engine::Engine *e)
            {
                for (uint32_t i = 0; i < lfosPerZone; ++i)
                {
                    sources[i] = SR{'znlf', 'outp', i};
                    registerVoiceModSource(e, sources[i], "", "LFO " + std::to_string(i + 1));
                }
            }
            std::array<SR, scxt::lfosPerZone> sources;
        } lfoSources;

        struct MIDISources
        {
            MIDISources(engine::Engine *e)
                : modWheelSource{'zmid', 'modw'}, velocitySource{'zmid', 'velo'}
            {
                registerVoiceModSource(e, modWheelSource, "MIDI", "Mod Wheel");
                registerVoiceModSource(e, velocitySource, "MIDI", "Velocity");
            }
            SR modWheelSource, velocitySource;
        } midiSources;

        SR aegSource, eg2Source;

        void bind(Matrix &m, engine::Zone &z, voice::Voice &v);

        float zeroSource{0.f};
    } sources;

    void bindTargetBaseValues(Matrix &m, engine::Zone &z);

    static void registerVoiceModTarget(engine::Engine *e, const MatrixConfig::TargetIdentifier &t,
                                       const std::string &path, const std::string &name)
    {
        registerVoiceModTarget(
            e, t, [p = path](const auto &a, const auto &b) -> std::string { return p; },
            [n = name](const auto &a, const auto &b) -> std::string { return n; });
    }

    static void registerVoiceModTarget(
        engine::Engine *e, const MatrixConfig::TargetIdentifier &,
        std::function<std::string(const engine::Zone &, const MatrixConfig::TargetIdentifier &)>
            pathFn,
        std::function<std::string(const engine::Zone &, const MatrixConfig::TargetIdentifier &)>
            nameFn);

    static void registerVoiceModSource(engine::Engine *e, const MatrixConfig::SourceIdentifier &t,
                                       const std::string &path, const std::string &name)
    {
        registerVoiceModSource(
            e, t, [p = path](const auto &a, const auto &b) -> std::string { return p; },
            [n = name](const auto &a, const auto &b) -> std::string { return n; });
    }

    static void registerVoiceModSource(
        engine::Engine *e, const MatrixConfig::SourceIdentifier &,
        std::function<std::string(const engine::Zone &, const MatrixConfig::SourceIdentifier &)>
            pathFn,
        std::function<std::string(const engine::Zone &, const MatrixConfig::SourceIdentifier &)>
            nameFn);
};

/*
 * Metadata functions return a path (or empty for top level) and name.
 * These are bad implementations for now
 */
typedef std::pair<std::string, std::string> identifierDisplayName_t;

typedef std::pair<MatrixConfig::TargetIdentifier, identifierDisplayName_t> namedTarget_t;
typedef std::vector<namedTarget_t> namedTargetVector_t;
typedef std::pair<MatrixConfig::SourceIdentifier, identifierDisplayName_t> namedSource_t;
typedef std::vector<namedSource_t> namedSourceVector_t;
typedef std::pair<MatrixConfig::CurveIdentifier, identifierDisplayName_t> namedCurve_t;
typedef std::vector<namedCurve_t> namedCurveVector_t;

typedef std::tuple<bool, namedSourceVector_t, namedTargetVector_t, namedCurveVector_t>
    voiceMatrixMetadata_t;

voiceMatrixMetadata_t getVoiceMatrixMetadata(engine::Zone &z);
} // namespace scxt::voice::modulation

#endif // __SCXT_VOICE_MATRIX_H
