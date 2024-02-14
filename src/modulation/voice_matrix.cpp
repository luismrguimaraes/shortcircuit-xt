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

#include <optional>

#include "datamodel/parameter.h"
#include "voice_matrix.h"
#include "engine/zone.h"
#include "voice/voice.h"

namespace scxt::voice::modulation
{
void bindEl(Matrix &m, Matrix::TR::TargetIdentifier &tg, float &tgs, const float *&p)
{
    assert(tg.gid != 0); // hit this? You forgot to init your target ctor
    assert(tg.tid != 0);
    m.bindTargetBaseValue(tg, tgs);
    p = m.getTargetValuePointer(tg);

    auto idxIt = m.routingTable.targetToOutputIndex.find(tg);
    if (idxIt != m.routingTable.targetToOutputIndex.end())
    {
        auto rg = tg.max - tg.min;
        auto pt = m.getTargetValuePointer(tg);
        for (auto &r : m.routingValuePointers)
        {
            if (r.target == pt)
            {
                r.depthScale = rg;
            }
        }
    }
};

void MatrixEndpoints::bindTargetBaseValues(scxt::voice::modulation::Matrix &m, engine::Zone &z)
{
    for (auto &l : lfo)
        l.bind(m, z);
    aeg.bind(m, z);
    eg2.bind(m, z);

    mappingTarget.bind(m, z);
    outputTarget.bind(m, z);

    for (auto &p : processorTarget)
        p.bind(m, z);
}
void MatrixEndpoints::LFOTarget::bind(scxt::voice::modulation::Matrix &m, engine::Zone &z)
{
    auto &ms = z.modulatorStorage[index];

    bindEl(m, rateT, ms.rate, rateP);
    bindEl(m, curveDeformT, ms.curveLfoStorage.deform, curveDeformP);
    bindEl(m, curveDelayT, ms.curveLfoStorage.delay, curveDelayP);
    bindEl(m, curveAttackT, ms.curveLfoStorage.attack, curveAttackP);
    bindEl(m, curveReleaseT, ms.curveLfoStorage.release, curveReleaseP);
    bindEl(m, stepSmoothT, ms.stepLfoStorage.smooth, stepSmoothP);
}

void MatrixEndpoints::EGTarget::bind(scxt::voice::modulation::Matrix &m, engine::Zone &z)
{
    auto doBind = [this, &m](auto &eg) {
        bindEl(m, aT, eg.a, aP);
        bindEl(m, hT, eg.h, hP);
        bindEl(m, dT, eg.d, dP);
        bindEl(m, sT, eg.s, sP);
        bindEl(m, rT, eg.r, rP);
        bindEl(m, asT, eg.aShape, asP);
        bindEl(m, dsT, eg.dShape, dsP);
        bindEl(m, rsT, eg.rShape, rsP);
    };

    if (index == 0)
    {
        doBind(z.aegStorage);
    }
    else if (index == 1)
    {
        doBind(z.eg2Storage);
    }
    else
    {
        assert(false);
    }
}

void MatrixEndpoints::MappingTarget::bind(scxt::voice::modulation::Matrix &m, engine::Zone &z)
{
    auto &mt = z.mapping;

    bindEl(m, pitchOffsetT, mt.pitchOffset, pitchOffsetP);
    bindEl(m, ampT, mt.amplitude, ampP);
    bindEl(m, panT, mt.pan, panP);
    bindEl(m, playbackRatioT, zeroBase, playbackRatioP);
}

void MatrixEndpoints::OutputTarget::bind(scxt::voice::modulation::Matrix &m, engine::Zone &z)
{
    auto &ot = z.outputInfo;
    bindEl(m, panT, ot.pan, panP);
    bindEl(m, ampT, ot.amplitude, ampP);
}

void MatrixEndpoints::ProcessorTarget::bind(scxt::voice::modulation::Matrix &m, engine::Zone &z)
{
    auto &p = z.processorStorage[index];
    auto &d = z.processorDescription[index];
    bindEl(m, mixT, p.mix, mixP);

    for (int i = 0; i < scxt::maxProcessorFloatParams; ++i)
    {
        auto &fcd = d.floatControlDescriptions[i];
        fpT[i].setMinMax(fcd.minVal, fcd.maxVal);
        bindEl(m, fpT[i], p.floatParams[i], floatP[i]);
    }
}

void MatrixEndpoints::Sources::bind(scxt::voice::modulation::Matrix &m, engine::Zone &z,
                                    voice::Voice &v)
{
    for (int i = 0; i < 3; ++i)
    {
        switch (v.lfoEvaluator[i])
        {
        case Voice::CURVE:
            m.bindSourceValue(lfoSources.sources[i], v.curveLfos[i].output);
            break;
        case Voice::STEP:
            m.bindSourceValue(lfoSources.sources[i], v.stepLfos[i].output);
            break;
        case Voice::MSEG:
        case Voice::ENV:
            m.bindSourceValue(lfoSources.sources[i], zeroSource);
            break;
        }
    }
    m.bindSourceValue(aegSource, v.aeg.outBlock0);
    m.bindSourceValue(eg2Source, v.eg2.outBlock0);

    m.bindSourceValue(midiSources.modWheelSource,
                      z.parentGroup->parentPart->midiCCSmoothers[1].output);
    m.bindSourceValue(midiSources.velocitySource, v.velocity);
}

void MatrixEndpoints::registerVoiceModTarget(
    engine::Engine *e, const MatrixConfig::TargetIdentifier &t,
    std::function<std::string(const engine::Zone &, const MatrixConfig::TargetIdentifier &)> pathFn,
    std::function<std::string(const engine::Zone &, const MatrixConfig::TargetIdentifier &)> nameFn)
{
    if (!e)
        return;

    e->registerVoiceModTarget(t, pathFn, nameFn);
}

void MatrixEndpoints::registerVoiceModSource(
    engine::Engine *e, const MatrixConfig::SourceIdentifier &t,
    std::function<std::string(const engine::Zone &, const MatrixConfig::SourceIdentifier &)> pathFn,
    std::function<std::string(const engine::Zone &, const MatrixConfig::SourceIdentifier &)> nameFn)
{
    if (!e)
        return;

    e->registerVoiceModSource(t, pathFn, nameFn);
}

voiceMatrixMetadata_t getVoiceMatrixMetadata(engine::Zone &z)
{
    auto e = z.getEngine();

    namedTargetVector_t tg;
    namedSourceVector_t sr;
    namedCurveVector_t cr;

    auto identCmp = [](const auto &a, const auto &b) {
        const auto &ida = a.second;
        const auto &idb = b.second;
        if (ida.first == idb.first)
        {
            if (ida.second == idb.second)
            {
                return std::hash<decltype(a.first)>{}(a.first) <
                       std::hash<decltype(b.first)>{}(b.first);
            }
            else
            {
                return ida.second < idb.second;
            }
        }
        return ida.first < idb.first;
    };

    auto tgtCmp = [identCmp](const auto &a, const auto &b) {
        const auto &ta = a.first;
        const auto &tb = b.first;
        if (ta.gid == 'proc' && tb.gid == ta.gid && tb.index == ta.index)
        {
            return ta.tid < tb.tid;
        }
        return identCmp(a, b);
    };

    for (const auto &[t, fns] : e->voiceModTargets)
    {
        tg.emplace_back(t, identifierDisplayName_t{fns.first(z, t), fns.second(z, t)});
    }
    std::sort(tg.begin(), tg.end(), tgtCmp);

    for (const auto &[s, fns] : e->voiceModSources)
    {
        sr.emplace_back(s, identifierDisplayName_t{fns.first(z, s), fns.second(z, s)});
    }
    std::sort(sr.begin(), sr.end(), identCmp);

    for (const auto &c : scxt::modulation::ModulationCurves::allCurves)
    {
        auto n = scxt::modulation::ModulationCurves::curveNames.find(c);
        assert(n != scxt::modulation::ModulationCurves::curveNames.end());
        cr.emplace_back(c, identifierDisplayName_t{"", n->second});
    }

    return voiceMatrixMetadata_t{true, sr, tg, cr};
}

MatrixEndpoints::ProcessorTarget::ProcessorTarget(engine::Engine *e, uint32_t(p))
    : index{p}, mixT{'proc', 'mix ', p}
{
    auto ptFn = [](const engine::Zone &z, const MatrixConfig::TargetIdentifier &t) -> std::string {
        auto &d = z.processorDescription[t.index];
        if (d.type == dsp::processor::proct_none)
            return "";
        return std::string("P") + std::to_string(t.index + 1) + " " + d.typeDisplayName;
    };

    auto mixFn = [](const engine::Zone &z, const MatrixConfig::TargetIdentifier &t) -> std::string {
        auto &d = z.processorDescription[t.index];
        if (d.type == dsp::processor::proct_none)
            return "";
        return "mix";
    };

    registerVoiceModTarget(e, mixT, ptFn, mixFn);
    for (int i = 0; i < scxt::maxProcessorFloatParams; ++i)
    {
        auto elFn = [icopy = i](const engine::Zone &z,
                                const MatrixConfig::TargetIdentifier &t) -> std::string {
            auto &d = z.processorDescription[t.index];
            if (d.type == dsp::processor::proct_none)
                return "";
            return d.floatControlDescriptions[icopy].name;
        };
        // the '0'+ structure is used in getdisplayname too
        fpT[i] = TG{'proc', 'fp  ' + (uint32_t)(i + '0' - ' '), p};
        registerVoiceModTarget(e, fpT[i], ptFn, elFn);
    }
}

MatrixEndpoints::LFOTarget::LFOTarget(engine::Engine *e, uint32_t p)
    : index(p), rateT{'lfo ', 'rate', p}, curveDeformT{'lfo ', 'cdfn', p},
      curveAttackT{'lfo ', 'catk', p}, curveDelayT{'lfo ', 'cdel', p},
      curveReleaseT{'lfo ', 'crel', p}, stepSmoothT{'lfo ', 'ssmt', p}
{
    if (e)
    {
        auto ptFn = [](const engine::Zone &z,
                       const MatrixConfig::TargetIdentifier &t) -> std::string {
            return "LFO " + std::to_string(t.index + 1);
        };

        auto conditionLabel =
            [](const std::string &lab,
               std::function<bool(const scxt::modulation::ModulatorStorage &)> op) {
                return [o = op, l = lab](const engine::Zone &z,
                                         const MatrixConfig::TargetIdentifier &t) -> std::string {
                    auto &ms = z.modulatorStorage[t.index];
                    if (o(ms))
                        return l;
                    return "";
                };
            };

        auto stepLabel = [conditionLabel](auto labl) {
            return conditionLabel(labl, [](auto &ms) { return ms.isStep(); });
        };

        auto curveLabel = [conditionLabel](auto labl) {
            return conditionLabel(labl, [](auto &ms) { return ms.isCurve(); });
        };

        auto envLabel = [conditionLabel](auto labl) {
            return conditionLabel(labl, [](auto &ms) { return ms.isEnv(); });
        };

        auto allLabel = [conditionLabel](auto labl) {
            return conditionLabel(labl, [](auto &ms) { return true; });
        };

        registerVoiceModTarget(e, rateT, ptFn, allLabel("Rate"));
        registerVoiceModTarget(e, curveDeformT, ptFn, allLabel("Curve Deform"));
        registerVoiceModTarget(e, curveDelayT, ptFn, allLabel("Curve Delay"));
        registerVoiceModTarget(e, curveAttackT, ptFn, allLabel("Curve Attack"));
        registerVoiceModTarget(e, curveReleaseT, ptFn, allLabel("Curve Release"));
        registerVoiceModTarget(e, stepSmoothT, ptFn, allLabel("Step Smooth"));
    }
}

} // namespace scxt::voice::modulation
