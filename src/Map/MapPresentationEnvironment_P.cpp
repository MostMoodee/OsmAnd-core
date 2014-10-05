#include "MapPresentationEnvironment_P.h"
#include "MapPresentationEnvironment.h"

#include <limits>

#include "ignore_warnings_on_external_includes.h"
#include <SkDashPathEffect.h>
#include <SkBitmapProcShader.h>
#include <SkImageDecoder.h>
#include <SkStream.h>
#include "restore_internal_warnings.h"

#include "UnresolvedMapStyle_P.h"
#include "MapStyleEvaluator.h"
#include "MapStyleEvaluationResult.h"
#include "MapStyleValueDefinition.h"
#include "MapStyleConstantValue.h"
#include "MapStyleBuiltinValueDefinitions.h"
#include "ObfMapSectionInfo.h"
#include "CoreResourcesEmbeddedBundle.h"
#include "ICoreResourcesProvider.h"
#include "QKeyValueIterator.h"
#include "Utilities.h"
#include "Logging.h"

OsmAnd::MapPresentationEnvironment_P::MapPresentationEnvironment_P(MapPresentationEnvironment* owner_)
    : owner(owner_)
    , dummyMapSection(new ObfMapSectionInfo())
{
}

OsmAnd::MapPresentationEnvironment_P::~MapPresentationEnvironment_P()
{
}

void OsmAnd::MapPresentationEnvironment_P::initialize()
{
    _defaultColorAttribute = owner->resolvedStyle->getAttribute(QLatin1String("defaultColor"));
    _defaultColor = ColorRGB(0xf1, 0xee, 0xe8);

    _shadowRenderingAttribute = owner->resolvedStyle->getAttribute(QLatin1String("shadowRendering"));
    _shadowRenderingMode = 0;
    _shadowRenderingColor = ColorRGB(0x96, 0x96, 0x96);

    _polygonMinSizeToDisplayAttribute = owner->resolvedStyle->getAttribute(QLatin1String("polygonMinSizeToDisplay"));
    _polygonMinSizeToDisplay = 0.0;

    _roadDensityZoomTileAttribute = owner->resolvedStyle->getAttribute(QLatin1String("roadDensityZoomTile"));
    _roadDensityZoomTile = 0;

    _roadsDensityLimitPerTileAttribute = owner->resolvedStyle->getAttribute(QLatin1String("roadsDensityLimitPerTile"));
    _roadsDensityLimitPerTile = 0;
}

QHash< OsmAnd::ResolvedMapStyle::ValueDefinitionId, OsmAnd::MapStyleConstantValue > OsmAnd::MapPresentationEnvironment_P::getSettings() const
{
    return _settings;
}

void OsmAnd::MapPresentationEnvironment_P::setSettings(const QHash< OsmAnd::ResolvedMapStyle::ValueDefinitionId, MapStyleConstantValue >& newSettings)
{
    QMutexLocker scopedLocker(&_settingsChangeMutex);

    _settings = newSettings;
}

void OsmAnd::MapPresentationEnvironment_P::setSettings(const QHash< QString, QString >& newSettings)
{
    QHash< ResolvedMapStyle::ValueDefinitionId, MapStyleConstantValue > resolvedSettings;
    resolvedSettings.reserve(newSettings.size());

    for (const auto& itSetting : rangeOf(newSettings))
    {
        const auto& name = itSetting.key();
        const auto& value = itSetting.value();

        // Resolve input-value definition by name
        const auto valueDefId = owner->resolvedStyle->getValueDefinitionIdByName(name);
        const auto valueDef = owner->resolvedStyle->getValueDefinitionById(valueDefId);
        if (!valueDef || valueDef->valueClass != MapStyleValueDefinition::Class::Input)
        {
            LogPrintf(LogSeverityLevel::Warning,
                "Setting of '%s' to '%s' impossible: failed to resolve input value definition failed with such name",
                qPrintable(name),
                qPrintable(value));
            continue;
        }

        // Parse value
        MapStyleConstantValue parsedValue;
        if (!owner->resolvedStyle->parseValue(value, valueDef, parsedValue))
        {
            LogPrintf(LogSeverityLevel::Warning,
                "Setting of '%s' to '%s' impossible: failed to parse value",
                qPrintable(name),
                qPrintable(value));
            continue;
        }

        resolvedSettings.insert(valueDefId, parsedValue);
    }

    setSettings(resolvedSettings);
}

void OsmAnd::MapPresentationEnvironment_P::applyTo(MapStyleEvaluator& evaluator) const
{
    QMutexLocker scopedLocker(&_settingsChangeMutex);

    for (const auto& settingEntry : rangeOf(constOf(_settings)))
    {
        const auto& valueDefId = settingEntry.key();
        const auto& settingValue = settingEntry.value();

        const auto valueDef = owner->resolvedStyle->getValueDefinitionById(valueDefId);
        if (!valueDef)
            continue;

        switch (valueDef->dataType)
        {
            case MapStyleValueDataType::Integer:
                evaluator.setIntegerValue(valueDefId,
                    settingValue.isComplex
                    ? settingValue.asComplex.asInt.evaluate(owner->displayDensityFactor)
                    : settingValue.asSimple.asInt);
                break;
            case MapStyleValueDataType::Float:
                evaluator.setFloatValue(valueDefId,
                    settingValue.isComplex
                    ? settingValue.asComplex.asFloat.evaluate(owner->displayDensityFactor)
                    : settingValue.asSimple.asFloat);
                break;
            case MapStyleValueDataType::Boolean:
            case MapStyleValueDataType::String:
            case MapStyleValueDataType::Color:
                assert(!settingValue.isComplex);
                evaluator.setIntegerValue(valueDefId, settingValue.asSimple.asUInt);
                break;
        }
    }
}

bool OsmAnd::MapPresentationEnvironment_P::obtainShaderBitmap(const QString& name, std::shared_ptr<const SkBitmap>& outShaderBitmap) const
{
    QMutexLocker scopedLocker(&_shadersBitmapsMutex);

    auto itShaderBitmap = _shadersBitmaps.constFind(name);
    if (itShaderBitmap == _shadersBitmaps.cend())
    {
        const auto shaderBitmapPath = QString::fromLatin1("map/shaders/%1.png").arg(name);

        // Get data from embedded resources
        const auto data = obtainResourceByName(shaderBitmapPath);

        // Decode bitmap for a shader
        const std::shared_ptr<SkBitmap> bitmap(new SkBitmap());
        SkMemoryStream dataStream(data.constData(), data.length(), false);
        if (!SkImageDecoder::DecodeStream(&dataStream, bitmap.get(), SkBitmap::Config::kNo_Config, SkImageDecoder::kDecodePixels_Mode))
            return false;
        itShaderBitmap = _shadersBitmaps.insert(name, bitmap);
    }

    // Create shader from that bitmap
    outShaderBitmap = *itShaderBitmap;
    return true;
}

bool OsmAnd::MapPresentationEnvironment_P::obtainMapIcon(const QString& name, std::shared_ptr<const SkBitmap>& outIcon) const
{
    QMutexLocker scopedLocker(&_mapIconsMutex);

    auto itIcon = _mapIcons.constFind(name);
    if (itIcon == _mapIcons.cend())
    {
        const auto bitmapPath = QString::fromLatin1("map/icons/%1.png").arg(name);

        // Get data from embedded resources
        auto data = obtainResourceByName(bitmapPath);

        // Decode data
        const std::shared_ptr<SkBitmap> bitmap(new SkBitmap());
        SkMemoryStream dataStream(data.constData(), data.length(), false);
        if (!SkImageDecoder::DecodeStream(&dataStream, bitmap.get(), SkBitmap::Config::kNo_Config, SkImageDecoder::kDecodePixels_Mode))
            return false;

        itIcon = _mapIcons.insert(name, bitmap);
    }

    outIcon = *itIcon;
    return true;
}

bool OsmAnd::MapPresentationEnvironment_P::obtainTextShield(const QString& name, std::shared_ptr<const SkBitmap>& outTextShield) const
{
    QMutexLocker scopedLocker(&_textShieldsMutex);

    auto itTextShield = _textShields.constFind(name);
    if (itTextShield == _textShields.cend())
    {
        const auto bitmapPath = QString::fromLatin1("map/shields/%1.png").arg(name);

        // Get data from embedded resources
        auto data = obtainResourceByName(bitmapPath);

        // Decode data
        const std::shared_ptr<SkBitmap> bitmap(new SkBitmap());
        SkMemoryStream dataStream(data.constData(), data.length(), false);
        if (!SkImageDecoder::DecodeStream(&dataStream, bitmap.get(), SkBitmap::Config::kNo_Config, SkImageDecoder::kDecodePixels_Mode))
            return false;

        itTextShield = _textShields.insert(name, bitmap);
    }

    outTextShield = *itTextShield;
    return true;
}

bool OsmAnd::MapPresentationEnvironment_P::obtainIconShield(const QString& name, std::shared_ptr<const SkBitmap>& outIconShield) const
{
    QMutexLocker scopedLocker(&_iconShieldsMutex);

    auto itIconShield = _iconShields.constFind(name);
    if (itIconShield == _iconShields.cend())
    {
        const auto bitmapPath = QString::fromLatin1("map/shields/%1.png").arg(name);

        // Get data from embedded resources
        auto data = obtainResourceByName(bitmapPath);

        // Decode data
        const std::shared_ptr<SkBitmap> bitmap(new SkBitmap());
        SkMemoryStream dataStream(data.constData(), data.length(), false);
        if (!SkImageDecoder::DecodeStream(&dataStream, bitmap.get(), SkBitmap::Config::kNo_Config, SkImageDecoder::kDecodePixels_Mode))
            return false;

        itIconShield = _iconShields.insert(name, bitmap);
    }

    outIconShield = *itIconShield;
    return true;
}

QByteArray OsmAnd::MapPresentationEnvironment_P::obtainResourceByName(const QString& name) const
{
    bool ok = false;

    // Try to obtain from external resources first
    if (static_cast<bool>(owner->externalResourcesProvider))
    {
        const auto resource = owner->externalResourcesProvider->getResource(name, owner->displayDensityFactor, &ok);
        if (ok)
            return resource;
    }

    // Otherwise obtain from global
    const auto resource = getCoreResourcesProvider()->getResource(name, owner->displayDensityFactor, &ok);
    if (!ok)
    {
        LogPrintf(LogSeverityLevel::Warning,
            "Resource '%s' (requested by MapPresentationEnvironment) was not found",
            qPrintable(name));
        return QByteArray();
    }
    return resource;
}

OsmAnd::ColorARGB OsmAnd::MapPresentationEnvironment_P::getDefaultBackgroundColor(const ZoomLevel zoom) const
{
    auto result = _defaultColor;

    if (_defaultColorAttribute)
    {
        MapStyleEvaluator evaluator(owner->resolvedStyle, owner->displayDensityFactor);
        applyTo(evaluator);
        evaluator.setIntegerValue(owner->styleBuiltinValueDefs->id_INPUT_MINZOOM, zoom);

        MapStyleEvaluationResult evalResult;
        if (evaluator.evaluate(_defaultColorAttribute, &evalResult))
            evalResult.getIntegerValue(owner->styleBuiltinValueDefs->id_OUTPUT_ATTR_COLOR_VALUE, result.argb);
    }

    return result;
}

void OsmAnd::MapPresentationEnvironment_P::obtainShadowRenderingOptions(const ZoomLevel zoom, int& mode, ColorARGB& color) const
{
    mode = _shadowRenderingMode;
    color = _shadowRenderingColor;

    if (_shadowRenderingAttribute)
    {
        MapStyleEvaluator evaluator(owner->resolvedStyle, owner->displayDensityFactor);
        applyTo(evaluator);
        evaluator.setIntegerValue(owner->styleBuiltinValueDefs->id_INPUT_MINZOOM, zoom);

        MapStyleEvaluationResult evalResult;
        if (evaluator.evaluate(_shadowRenderingAttribute, &evalResult))
        {
            evalResult.getIntegerValue(owner->styleBuiltinValueDefs->id_OUTPUT_ATTR_INT_VALUE, mode);
            evalResult.getIntegerValue(owner->styleBuiltinValueDefs->id_OUTPUT_SHADOW_COLOR, color.argb);
        }
    }
}

double OsmAnd::MapPresentationEnvironment_P::getPolygonAreaMinimalThreshold(const ZoomLevel zoom) const
{
    auto result = _polygonMinSizeToDisplay;

    if (_polygonMinSizeToDisplayAttribute)
    {
        MapStyleEvaluator evaluator(owner->resolvedStyle, owner->displayDensityFactor);
        applyTo(evaluator);
        evaluator.setIntegerValue(owner->styleBuiltinValueDefs->id_INPUT_MINZOOM, zoom);

        MapStyleEvaluationResult evalResult;
        if (evaluator.evaluate(_polygonMinSizeToDisplayAttribute, &evalResult))
        {
            int polygonMinSizeToDisplay;
            if (evalResult.getIntegerValue(owner->styleBuiltinValueDefs->id_OUTPUT_ATTR_INT_VALUE, polygonMinSizeToDisplay))
                result = polygonMinSizeToDisplay;
        }
    }

    return result;
}

unsigned int OsmAnd::MapPresentationEnvironment_P::getRoadDensityZoomTile(const ZoomLevel zoom) const
{
    auto result = _roadDensityZoomTile;

    if (_roadDensityZoomTileAttribute)
    {
        MapStyleEvaluator evaluator(owner->resolvedStyle, owner->displayDensityFactor);
        applyTo(evaluator);
        evaluator.setIntegerValue(owner->styleBuiltinValueDefs->id_INPUT_MINZOOM, zoom);

        MapStyleEvaluationResult evalResult;
        if (evaluator.evaluate(_roadDensityZoomTileAttribute, &evalResult))
            evalResult.getIntegerValue(owner->styleBuiltinValueDefs->id_OUTPUT_ATTR_INT_VALUE, result);
    }

    return result;
}

unsigned int OsmAnd::MapPresentationEnvironment_P::getRoadsDensityLimitPerTile(const ZoomLevel zoom) const
{
    auto result = _roadsDensityLimitPerTile;

    if (_roadsDensityLimitPerTileAttribute)
    {
        MapStyleEvaluator evaluator(owner->resolvedStyle, owner->displayDensityFactor);
        applyTo(evaluator);
        evaluator.setIntegerValue(owner->styleBuiltinValueDefs->id_INPUT_MINZOOM, zoom);

        MapStyleEvaluationResult evalResult;
        if (evaluator.evaluate(_roadsDensityLimitPerTileAttribute, &evalResult))
            evalResult.getIntegerValue(owner->styleBuiltinValueDefs->id_OUTPUT_ATTR_INT_VALUE, result);
    }

    return result;
}