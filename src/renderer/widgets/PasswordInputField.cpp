#include "PasswordInputField.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include "../../auth/Auth.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../config/ConfigManager.hpp"
#include "../../helpers/Log.hpp"
#include "../../core/AnimationManager.hpp"
#include "../../helpers/Color.hpp"
#include <cmath>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/string/String.hpp>
#include <algorithm>
#include <hyprlang.hpp>

using namespace Hyprutils::String;

CPasswordInputField::~CPasswordInputField() {
    reset();
}

void CPasswordInputField::registerSelf(const ASP<CPasswordInputField>& self) {
    m_self = self;
}

void CPasswordInputField::configure(const std::unordered_map<std::string, std::any>& prop, const SP<COutput>& pOutput) {
    reset();

    outputStringPort = pOutput->stringPort;
    viewport         = pOutput->getViewport();

    shadow.configure(m_self, prop, viewport);

    try {
        pos                      = CLayoutValueData::fromAnyPv(prop.at("position"))->getAbsolute(viewport);
        configSize               = CLayoutValueData::fromAnyPv(prop.at("size"))->getAbsolute(viewport);
        halign                   = std::any_cast<Hyprlang::STRING>(prop.at("halign"));
        valign                   = std::any_cast<Hyprlang::STRING>(prop.at("valign"));
        outThick                 = std::any_cast<Hyprlang::INT>(prop.at("outline_thickness"));
        dots.size                = std::any_cast<Hyprlang::FLOAT>(prop.at("dots_size"));
        dots.spacing             = std::any_cast<Hyprlang::FLOAT>(prop.at("dots_spacing"));
        dots.center              = std::any_cast<Hyprlang::INT>(prop.at("dots_center"));
        dots.rounding            = std::any_cast<Hyprlang::INT>(prop.at("dots_rounding"));
        dots.textFormat          = std::any_cast<Hyprlang::STRING>(prop.at("dots_text_format"));
        fadeOnEmpty              = std::any_cast<Hyprlang::INT>(prop.at("fade_on_empty"));
        fadeTimeoutMs            = std::any_cast<Hyprlang::INT>(prop.at("fade_timeout"));
        hiddenInputState.enabled = std::any_cast<Hyprlang::INT>(prop.at("hide_input"));
        rounding                 = std::any_cast<Hyprlang::INT>(prop.at("rounding"));
        configPlaceholderText    = std::any_cast<Hyprlang::STRING>(prop.at("placeholder_text"));
        configFailText           = std::any_cast<Hyprlang::STRING>(prop.at("fail_text"));
        configCheckingText       = prop.count("checking_text") ? std::any_cast<Hyprlang::STRING>(prop.at("checking_text")) : std::string("Checking...");
        fontFamily               = std::any_cast<Hyprlang::STRING>(prop.at("font_family"));
        colorConfig.outer        = CGradientValueData::fromAnyPv(prop.at("outer_color"));
        colorConfig.inner        = std::any_cast<Hyprlang::INT>(prop.at("inner_color"));
        colorConfig.font         = std::any_cast<Hyprlang::INT>(prop.at("font_color"));
        colorConfig.fail         = CGradientValueData::fromAnyPv(prop.at("fail_color"));
        colorConfig.check        = CGradientValueData::fromAnyPv(prop.at("check_color"));
        colorConfig.both         = CGradientValueData::fromAnyPv(prop.at("bothlock_color"));
        colorConfig.caps         = CGradientValueData::fromAnyPv(prop.at("capslock_color"));
        colorConfig.num          = CGradientValueData::fromAnyPv(prop.at("numlock_color"));
        colorConfig.invertNum    = std::any_cast<Hyprlang::INT>(prop.at("invert_numlock"));
        colorConfig.swapFont     = std::any_cast<Hyprlang::INT>(prop.at("swap_font_color"));
        colorConfig.hiddenBase   = std::any_cast<Hyprlang::INT>(prop.at("hide_input_base_color"));
        // Do NOT reset showCheckingOverlay or checkingOverlayTimer here; only do so on auth start, timer expiry, or Enter
    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CPasswordInputField: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CPasswordInputField: {}", e.what()); //
    }

    configPos       = pos;
    colorState.font = colorConfig.font;

    pos          = posFromHVAlign(viewport, configSize, pos, halign, valign);
    dots.size    = std::clamp(dots.size, 0.2f, 0.8f);
    dots.spacing = std::clamp(dots.spacing, -1.f, 1.f);

    colorConfig.caps = colorConfig.caps->m_bIsFallback ? colorConfig.fail : colorConfig.caps;

    g_pAnimationManager->createAnimation(0.f, fade.a, g_pConfigManager->m_AnimationTree.getConfig("inputFieldFade"));
    g_pAnimationManager->createAnimation(0.f, dots.currentAmount, g_pConfigManager->m_AnimationTree.getConfig("inputFieldDots"));
    g_pAnimationManager->createAnimation(configSize, size, g_pConfigManager->m_AnimationTree.getConfig("inputFieldWidth"));
    g_pAnimationManager->createAnimation(colorConfig.inner, colorState.inner, g_pConfigManager->m_AnimationTree.getConfig("inputFieldColors"));
    g_pAnimationManager->createAnimation(*colorConfig.outer, colorState.outer, g_pConfigManager->m_AnimationTree.getConfig("inputFieldColors"));

    srand(std::chrono::system_clock::now().time_since_epoch().count());

    pos = posFromHVAlign(viewport, size->goal(), configPos, halign, valign);

    if (!dots.textFormat.empty()) {
        dots.textResourceID = "input:" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + dots.textFormat;
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id                   = dots.textResourceID;
        request.asset                = dots.textFormat;
        request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        request.props["font_family"] = fontFamily;
        request.props["color"]       = colorConfig.font;
        request.props["font_size"]   = (int)(std::nearbyint(configSize.y * dots.size * 0.5f) * 2.f);

        g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
    }

    // request the inital placeholder asset
    updatePlaceholder();
}

void CPasswordInputField::reset() {
    if (fade.fadeOutTimer.get()) {
        fade.fadeOutTimer->cancel();
        fade.fadeOutTimer.reset();
    }

    if (g_pHyprlock->m_bTerminate)
        return;

    if (placeholder.asset)
        g_pRenderer->asyncResourceGatherer->unloadAsset(placeholder.asset);

    // PATCH: Clean up fail text asset if present
    std::string failText = formatString(configFailText).formatted;
    std::string failResourceID = "failtext:" + failText +
        std::to_string(reinterpret_cast<uintptr_t>(this)) +
        std::to_string(colorConfig.font.r) +
        std::to_string(colorConfig.font.g) +
        std::to_string(colorConfig.font.b) +
        std::to_string(colorConfig.font.a);
    if (auto failAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(failResourceID); failAsset)
        g_pRenderer->asyncResourceGatherer->unloadAsset(failAsset);

    placeholder.asset = nullptr;
    placeholder.resourceID.clear();
    placeholder.currentText.clear();
}

static void fadeOutCallback(AWP<CPasswordInputField> ref) {
    if (const auto PP = ref.lock(); PP)
        PP->onFadeOutTimer();
}

void CPasswordInputField::onFadeOutTimer() {
    fade.allowFadeOut = true;
    fade.fadeOutTimer.reset();

    g_pHyprlock->renderOutput(outputStringPort);
}

void CPasswordInputField::updateFade() {
    if (!fadeOnEmpty) {
        fade.a->setValueAndWarp(1.0);
        return;
    }

    const bool INPUTUSED = passwordLength > 0 || checkWaiting;

    if (INPUTUSED && fade.allowFadeOut)
        fade.allowFadeOut = false;

    if (INPUTUSED && fade.fadeOutTimer.get()) {
        fade.fadeOutTimer->cancel();
        fade.fadeOutTimer.reset();
    }

    if (!INPUTUSED && fade.a->goal() != 0.0) {
        if (fade.allowFadeOut || fadeTimeoutMs == 0) {
            *fade.a           = 0.0;
            fade.allowFadeOut = false;
        } else if (!fade.fadeOutTimer.get())
            fade.fadeOutTimer = g_pHyprlock->addTimer(std::chrono::milliseconds(fadeTimeoutMs), [REF = m_self](auto, auto) { fadeOutCallback(REF); }, nullptr);

    } else if (INPUTUSED && fade.a->goal() != 1.0)
        *fade.a = 1.0;

    if (fade.a->isBeingAnimated())
        redrawShadow = true;
}

void CPasswordInputField::updateDots() {
    if (dots.currentAmount->goal() == passwordLength)
        return;

    if (passwordLength == 0)
        dots.currentAmount->setValueAndWarp(passwordLength);
    else
        *dots.currentAmount = passwordLength;
}

bool CPasswordInputField::draw(const SRenderData& data) {
    if (firstRender || redrawShadow) {
        firstRender  = false;
        redrawShadow = false;
        shadow.markShadowDirty();
    }

    bool forceReload = false;

    passwordLength = g_pHyprlock->getPasswordBufferDisplayLen();
    checkWaiting   = g_pAuth->checkWaiting();
    displayFail    = g_pAuth->m_bDisplayFailText;



    // Show overlay if just started checking
    static bool prevCheckWaiting = false;
    if (checkWaiting && !prevCheckWaiting) {
        showCheckingOverlay = true;
        // Cancel previous timer if any (only if a new auth starts)
        if (checkingOverlayTimer)
            checkingOverlayTimer->cancel();
        // Set timer to hide overlay after 2 seconds
        checkingOverlayTimer = g_pHyprlock->addTimer(std::chrono::milliseconds(2000), [](ASP<CTimer> self, void* data) {
            if (auto* pThis = static_cast<CPasswordInputField*>(data)) {
                pThis->showCheckingOverlay = false;
                pThis->checkingOverlayTimer.reset();
                g_pHyprlock->renderAllOutputs();
            }
        }, this);
    }
    // Do NOT hide/cancel overlay or timer on any other state change
    prevCheckWaiting = checkWaiting;

    prevCheckWaiting = checkWaiting;

    showChecking   = checkWaiting;
    displayPasswordLength = passwordLength;

    // --- PATCH: Render fail text above input box ---
    if (displayFail && !configFailText.empty()) {
        // Generate a unique resource ID for the fail text asset (use fail color for uniqueness)
        std::string failText = formatString(configFailText).formatted;
        CHyprColor failCol = (colorConfig.fail && !colorConfig.fail->m_vColors.empty())
            ? colorConfig.fail->m_vColors[0]
            : CHyprColor(1.0, 0.0, 0.0, 1.0);
        std::string failResourceID = "failtext:" + failText +
            std::to_string(reinterpret_cast<uintptr_t>(this)) +
            std::to_string(failCol.r) +
            std::to_string(failCol.g) +
            std::to_string(failCol.b) +
            std::to_string(failCol.a);

        // Request asset if not already present
        SPreloadedAsset* failAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(failResourceID);
        if (!failAsset) {
            CAsyncResourceGatherer::SPreloadRequest request;
            request.id = failResourceID;
            request.asset = failText;
            request.type = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
            request.props["font_family"] = fontFamily;
            request.props["color"] = failCol;
            request.props["font_size"] = (int)size->value().y / 4;
            request.callback = [m_self = m_self] {
                if (const auto SELF = m_self.lock(); SELF)
                    g_pHyprlock->renderOutput(SELF->outputStringPort);
            };
            g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
            forceReload = true;
        } else {
            // Calculate position above the input box (subtract height and padding)
            Vector2D failSize = failAsset->texture.m_vSize;
            double padding = size->value().y * 0.25;
            Vector2D failPos = pos + Vector2D{(size->value().x - failSize.x) / 2.0, size->value().y + padding};
            CBox failBox{failPos, failSize};
            g_pRenderer->renderTexture(failBox, failAsset->texture, data.opacity * fade.a->value(), 0);
        }
    }
    // --- END PATCH ---

    updateFade();
    updateDots();
    updateColors();
    updatePlaceholder();
    updateWidth();
    updateHiddenInputState();

    CBox        inputFieldBox = {pos, size->value()};
    CBox        outerBox      = {pos - Vector2D{outThick, outThick}, size->value() + Vector2D{outThick * 2, outThick * 2}};

    SRenderData shadowData = data;
    shadowData.opacity *= fade.a->value();

    if (!size->isBeingAnimated())
        shadow.draw(shadowData);

    //CGradientValueData outerGrad = colorState.outer->value();
    //for (auto& c : outerGrad.m_vColors)
    //    c.a *= fade.a->value() * data.opacity;

    CHyprColor innerCol = colorState.inner->value();
    innerCol.a *= fade.a->value() * data.opacity;
    CHyprColor fontCol = colorState.font;
    fontCol.a *= fade.a->value() * data.opacity;

    if (outThick > 0) {
        const auto OUTERROUND = roundingForBorderBox(outerBox, rounding, outThick);
        CHyprColor borderCol = checkWaiting ? CHyprColor(1.0, 0.75, 0.0, 1.0) : 
            (colorState.outer->value().m_vColors.empty() ? CHyprColor(1.0, 1.0, 1.0, 1.0) : colorState.outer->value().m_vColors[0]);
        g_pRenderer->renderBorder(outerBox, borderCol, outThick, OUTERROUND, fade.a->value() * data.opacity);

        if (passwordLength != 0 && !checkWaiting && hiddenInputState.enabled) {
            CBox     outerBoxScaled = outerBox;
            Vector2D p              = outerBox.pos();
            outerBoxScaled.translate(-p).scale(0.5).translate(p);
            if (hiddenInputState.lastQuadrant > 1)
                outerBoxScaled.y += outerBoxScaled.h;
            if (hiddenInputState.lastQuadrant % 2 == 1)
                outerBoxScaled.x += outerBoxScaled.w;
            glEnable(GL_SCISSOR_TEST);
            glScissor(outerBoxScaled.x, outerBoxScaled.y, outerBoxScaled.w, outerBoxScaled.h);
            g_pRenderer->renderBorder(outerBox, hiddenInputState.lastColor, outThick, OUTERROUND, fade.a->value() * data.opacity);
            glScissor(0, 0, viewport.x, viewport.y);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    const int ROUND = roundingForBox(inputFieldBox, rounding);
    g_pRenderer->renderRect(inputFieldBox, innerCol, ROUND);

    if (!hiddenInputState.enabled) {
        const int RECTPASSSIZE = std::nearbyint(inputFieldBox.h * dots.size * 0.5f) * 2.f;
        Vector2D  passSize{RECTPASSSIZE, RECTPASSSIZE};
        int       passSpacing = std::floor(passSize.x * dots.spacing);

        if (!dots.textFormat.empty()) {
            if (!dots.textAsset)
                dots.textAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(dots.textResourceID);

            if (!dots.textAsset)
                forceReload = true;
            else {
                passSize    = dots.textAsset->texture.m_vSize;
                passSpacing = std::floor(passSize.x * dots.spacing);
            }
        }

        const auto   CURRDOTS     = dots.currentAmount->value();
        const double DOTPAD       = (inputFieldBox.h - passSize.y) / 2.0;
        const double DOTAREAWIDTH = inputFieldBox.w - (DOTPAD * 2);
        const int    MAXDOTS      = std::round(DOTAREAWIDTH * 1.0 / (passSize.x + passSpacing));
        const int    DOTFLOORED   = std::floor(CURRDOTS);
        const auto   DOTALPHA     = fontCol.a;

        // Calculate the total width required for all dots including spaces between them
        const double CURRWIDTH = ((passSize.x + passSpacing) * CURRDOTS) - passSpacing;

        // Calculate starting x-position to ensure dots stay centered within the input field
        double xstart = dots.center ? ((DOTAREAWIDTH - CURRWIDTH) / 2.0) + DOTPAD : DOTPAD;

        if (CURRDOTS > MAXDOTS)
            xstart = (inputFieldBox.w + MAXDOTS * (passSize.x + passSpacing) - passSpacing - 2 * CURRWIDTH) / 2.0;

        if (dots.rounding == -1)
            dots.rounding = passSize.x / 2.0;
        else if (dots.rounding == -2)
            dots.rounding = rounding == -1 ? passSize.x / 2.0 : rounding * dots.size;

        for (int i = 0; i < CURRDOTS; ++i) {
            if (i < DOTFLOORED - MAXDOTS)
                continue;

            if (CURRDOTS != DOTFLOORED) {
                if (i == DOTFLOORED)
                    fontCol.a *= (CURRDOTS - DOTFLOORED) * data.opacity;
                else if (i == DOTFLOORED - MAXDOTS)
                    fontCol.a *= (1 - CURRDOTS + DOTFLOORED) * data.opacity;
            }

            Vector2D dotPosition = inputFieldBox.pos() + Vector2D{xstart + (i * (passSize.x + passSpacing)), (inputFieldBox.h / 2.0) - (passSize.y / 2.0)};
            CBox     box{dotPosition, passSize};
            if (!dots.textFormat.empty()) {
                if (!dots.textAsset) {
                    forceReload = true;
                    fontCol.a   = DOTALPHA;
                    break;
                }

                g_pRenderer->renderTexture(box, dots.textAsset->texture, fontCol.a, dots.rounding);
            } else
                g_pRenderer->renderRect(box, fontCol, dots.rounding);

            fontCol.a = DOTALPHA;
        }
    }

    // Always request and show placeholder text inside the box if input is empty
    if (displayPasswordLength == 0 && !placeholder.resourceID.empty()) {
        SPreloadedAsset* currAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(placeholder.resourceID);

        // If asset is missing, request it
        if (!currAsset) {
            CAsyncResourceGatherer::SPreloadRequest request;
            request.id = placeholder.resourceID;
            request.asset = placeholder.currentText;
            request.type = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
            request.props["font_family"] = fontFamily;
            request.props["color"] = colorState.font;
            request.props["font_size"] = (int)size->value().y / 4;
            request.callback = [m_self = m_self] {
                if (const auto SELF = m_self.lock(); SELF)
                    g_pHyprlock->renderOutput(SELF->outputStringPort);
            };
            g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
            forceReload = true;
        } else {
            const Vector2D ASSETPOS = inputFieldBox.pos() + inputFieldBox.size() / 2.0 - currAsset->texture.m_vSize / 2.0;
            const CBox     ASSETBOX{ASSETPOS, currAsset->texture.m_vSize};

            // Cut the texture to the width of the input field
            glEnable(GL_SCISSOR_TEST);
            glScissor(inputFieldBox.x, inputFieldBox.y, inputFieldBox.w, inputFieldBox.h);
            g_pRenderer->renderTexture(ASSETBOX, currAsset->texture, data.opacity * fade.a->value(), 0);
            glScissor(0, 0, viewport.x, viewport.y);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    // Only show one overlay above the input box: fail > checking > none
    if (displayFail && !configFailText.empty()) {
        // (fail text rendering already handled above)
    } else if (showCheckingOverlay && !configCheckingText.empty()) {
        std::string checkingResourceID = "checkingtext:" + configCheckingText +
            std::to_string(reinterpret_cast<uintptr_t>(this)) +
            std::to_string(colorState.font.r) +
            std::to_string(colorState.font.g) +
            std::to_string(colorState.font.b) +
            std::to_string(colorState.font.a);

        SPreloadedAsset* checkingAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(checkingResourceID);
        if (!checkingAsset) {
            CAsyncResourceGatherer::SPreloadRequest request;
            request.id = checkingResourceID;
            request.asset = configCheckingText;
            request.type = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
            request.props["font_family"] = fontFamily;
            request.props["color"] = colorState.font;
            request.props["font_size"] = (int)size->value().y / 4;
            request.callback = [m_self = m_self] {
                if (const auto SELF = m_self.lock(); SELF)
                    g_pHyprlock->renderOutput(SELF->outputStringPort);
            };
            g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
            forceReload = true;
        } else {
            // Place above the input box, like fail text
            Vector2D checkingSize = checkingAsset->texture.m_vSize;
            double padding = size->value().y * 0.25;
            Vector2D checkingPos = pos + Vector2D{(size->value().x - checkingSize.x) / 2.0, size->value().y + padding};
            CBox checkingBox{checkingPos, checkingSize};
            g_pRenderer->renderTexture(checkingBox, checkingAsset->texture, data.opacity * fade.a->value(), 0);
        }
    }

    return redrawShadow || forceReload;
}

void CPasswordInputField::setShowCheckingOverlay(bool show) {
    showCheckingOverlay = show;
    if (!show && checkingOverlayTimer) {
        checkingOverlayTimer->cancel();
        checkingOverlayTimer.reset();
    }
    // Do NOT set showCheckingOverlay = false or cancel timer anywhere else
}

void CPasswordInputField::updatePlaceholder() {
    if (passwordLength != 0) {
        if (placeholder.asset && /* keep prompt asset cause it is likely to be used again */ displayFail) {
            std::erase(placeholder.registeredResourceIDs, placeholder.resourceID);
            g_pRenderer->asyncResourceGatherer->unloadAsset(placeholder.asset);
            placeholder.asset      = nullptr;
            placeholder.resourceID = "";
            redrawShadow           = true;
        }
        return;
    }

    // already requested a placeholder for the current fail
    if (displayFail && placeholder.failedAttempts == g_pAuth->getFailedAttempts())
        return;

    placeholder.failedAttempts = g_pAuth->getFailedAttempts();

    std::string newText = formatString(configPlaceholderText).formatted;

    // if the text is unchanged we don't need to do anything, unless we are swapping font color
    const auto ALLOWCOLORSWAP = outThick == 0 && colorConfig.swapFont;
    if (!ALLOWCOLORSWAP && newText == placeholder.currentText)
        return;

    const auto NEWRESOURCEID =
        "placeholder:" + placeholder.currentText +
        std::to_string(reinterpret_cast<uintptr_t>(this)) +
        std::to_string(colorState.font.r) +
        std::to_string(colorState.font.g) +
        std::to_string(colorState.font.b) +
        std::to_string(colorState.font.a);

    if (placeholder.resourceID == NEWRESOURCEID)
        return;

    Debug::log(TRACE, "Updating placeholder text: {}", newText);
    placeholder.currentText = newText;
    placeholder.asset       = nullptr;
    placeholder.resourceID  = NEWRESOURCEID;

    if (std::ranges::find(placeholder.registeredResourceIDs, placeholder.resourceID) != placeholder.registeredResourceIDs.end())
        return;

    Debug::log(TRACE, "Requesting new placeholder asset: {}", placeholder.resourceID);
    placeholder.registeredResourceIDs.push_back(placeholder.resourceID);

    // query
    CAsyncResourceGatherer::SPreloadRequest request;
    request.id                   = placeholder.resourceID;
    request.asset                = placeholder.currentText;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = fontFamily;
    request.props["color"]       = colorState.font;
    request.props["font_size"]   = (int)size->value().y / 4;
    request.callback             = [REF = m_self] {
        if (const auto SELF = REF.lock(); SELF)
            g_pHyprlock->renderOutput(SELF->outputStringPort);
    };
    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CPasswordInputField::updateWidth() {
    double targetSizeX = configSize.x;

    if (passwordLength == 0 && placeholder.asset)
        targetSizeX = placeholder.asset->texture.m_vSize.x + size->goal().y;

    targetSizeX = std::max(targetSizeX, configSize.x);

    if (size->goal().x != targetSizeX) {
        *size = Vector2D{targetSizeX, configSize.y};
        size->setCallbackOnEnd([this](auto) {
            redrawShadow = true;
            pos          = posFromHVAlign(viewport, size->value(), configPos, halign, valign);
        });
    }

    if (size->isBeingAnimated()) {
        redrawShadow = true;
        pos          = posFromHVAlign(viewport, size->value(), configPos, halign, valign);
    }
}

void CPasswordInputField::updateHiddenInputState() {
    if (!hiddenInputState.enabled || (size_t)hiddenInputState.lastPasswordLength == passwordLength)
        return;

    // randomize new thang
    hiddenInputState.lastPasswordLength = passwordLength;

    const auto BASEOK = colorConfig.hiddenBase.asOkLab();

    // convert to polar coordinates
    const auto OKICHCHROMA = std::sqrt(std::pow(BASEOK.a, 2) + std::pow(BASEOK.b, 2));

    // now randomly rotate the hue
    const double OKICHHUE = (rand() % 10000000 / 10000000.0) * M_PI * 4;

    // convert back to OkLab
    const Hyprgraphics::CColor newColor = Hyprgraphics::CColor::SOkLab{
        .l = BASEOK.l,
        .a = OKICHCHROMA * std::cos(OKICHHUE),
        .b = OKICHCHROMA * std::sin(OKICHHUE),
    };

    hiddenInputState.lastColor    = {newColor, 1.0};
    hiddenInputState.lastQuadrant = (hiddenInputState.lastQuadrant + rand() % 3 + 1) % 4;
}

void CPasswordInputField::updateColors() {
    const bool          BORDERLESS = outThick == 0;
    const bool          NUMLOCK    = (colorConfig.invertNum) ? !g_pHyprlock->m_bNumLock : g_pHyprlock->m_bNumLock;

    CGradientValueData* targetGrad = nullptr;

    if (g_pHyprlock->m_bCapsLock && NUMLOCK && !colorConfig.both->m_bIsFallback)
        targetGrad = colorConfig.both;
    else if (g_pHyprlock->m_bCapsLock)
        targetGrad = colorConfig.caps;
    else if (NUMLOCK && !colorConfig.num->m_bIsFallback)
        targetGrad = colorConfig.num;

    if (checkWaiting)
        targetGrad = colorConfig.check;
    else if (displayFail && passwordLength == 0)
        targetGrad = colorConfig.fail;

    CGradientValueData* outerTarget = colorConfig.outer;
    CHyprColor          innerTarget = colorConfig.inner;
    CHyprColor          fontTarget  = (displayFail) ? colorConfig.fail->m_vColors.front() : colorConfig.font;

    if (targetGrad) {
        if (BORDERLESS && colorConfig.swapFont) {
            fontTarget = targetGrad->m_vColors.front();
        } else if (BORDERLESS && !colorConfig.swapFont) {
            innerTarget = targetGrad->m_vColors.front();
            // When changing the inner color, the font cannot be fail_color
            fontTarget = colorConfig.font;
        } else if (targetGrad) {
            outerTarget = targetGrad;
        }
    }

    if (!BORDERLESS && *outerTarget != colorState.outer->goal())
        *colorState.outer = *outerTarget;

    if (innerTarget != colorState.inner->goal())
        *colorState.inner = innerTarget;

    colorState.font = fontTarget;
}

CBox CPasswordInputField::getBoundingBoxWl() const {
    return {
        Vector2D{pos.x, viewport.y - pos.y - size->value().y},
        size->value(),
    };
}

void CPasswordInputField::onHover(const Vector2D& pos) {
    g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
}
