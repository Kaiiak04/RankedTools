#include "account_view.hpp"
#include "account.hpp"
#include "main.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "bsml/shared/BSML/Components/ScrollViewContent.hpp"
#include "conditional-dependencies/shared/main.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/LayoutRebuilder.hpp"

#include <array>
#include <memory>
#include <optional>
#include <thread>

namespace rankedpractice {
namespace {

struct AccountRow {
    SafePtrUnity<UnityEngine::GameObject> object;
    SafePtrUnity<HMUI::CurvedTextMeshPro> text;
    SafePtrUnity<HMUI::ImageView> image;
    SafePtrUnity<UnityEngine::Sprite> sprite;
    SafePtrUnity<UnityEngine::Texture2D> texture;
};

struct AccountPanel {
    AccountService service;
    AccountLookupState lookup;
    std::string input, status;
    bool open{false}, showId{false};
    std::uint64_t metadataRevision{0};
    SafePtrUnity<HMUI::CurvedTextMeshPro> selectedText, statusText;
    SafePtrUnity<UnityEngine::GameObject> picker;
    SafePtrUnity<UnityEngine::UI::Button> find, previous, next, signedIn;
    std::array<AccountRow, 5> rows;
    std::array<std::uint64_t, 5> rowRevisions{};
};

std::array<std::shared_ptr<AccountPanel>, 2> panels;
SafePtrUnity<UnityEngine::RectTransform> settingsContentRect;
SafePtrUnity<BSML::ScrollViewContent> settingsScrollContent;

std::string SavedId(AccountService service) {
    return service == AccountService::BeatLeader ? getModConfig().BeatLeaderPlayerId.GetValue()
                                                : getModConfig().ScoreSaberPlayerId.GetValue();
}

std::string SavedName(AccountService service) {
    return service == AccountService::BeatLeader ? getModConfig().BeatLeaderPlayerName.GetValue()
                                                : getModConfig().ScoreSaberPlayerName.GetValue();
}

void SaveName(AccountService service, const std::string& name) {
    if (SavedName(service) == name) return;
    if (service == AccountService::BeatLeader) getModConfig().BeatLeaderPlayerName.SetValue(name);
    else getModConfig().ScoreSaberPlayerName.SetValue(name);
}

void RebuildSettings() {
    if (settingsContentRect) UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(settingsContentRect.ptr());
    if (settingsScrollContent) settingsScrollContent->UpdateScrollView();
}

void ReleaseAvatar(AccountRow& row) {
    if (row.image) { row.image->set_sprite(nullptr); row.image->set_enabled(false); }
    if (row.sprite) UnityEngine::Object::Destroy(row.sprite.ptr());
    if (row.texture) UnityEngine::Object::Destroy(row.texture.ptr());
    row.sprite = nullptr; row.texture = nullptr;
}

void UpdatePanel(const std::shared_ptr<AccountPanel>& panel) {
    if (!panel->selectedText || !panel->statusText || !panel->picker ||
        !panel->find || !panel->previous || !panel->next) return;
    const auto id = SavedId(panel->service), name = SavedName(panel->service);
    const auto service = panel->service == AccountService::BeatLeader ? "BeatLeader" : "ScoreSaber";
    std::string selected = std::string(service) + " account: " +
        (id.empty() ? "Not selected" : name.empty() ? "Saved account" : name);
    if (panel->showId && !id.empty()) selected += "\nID: " + id;
    panel->selectedText->set_text(StringW(selected));
    panel->picker->SetActive(panel->open);
    panel->statusText->set_text(StringW(panel->status));
    panel->find->set_interactable(!panel->lookup.loading);
    if (panel->signedIn) panel->signedIn->set_interactable(!panel->lookup.loading);
    panel->previous->set_interactable(!panel->lookup.loading && panel->lookup.results.page > 1);
    panel->next->set_interactable(!panel->lookup.loading && panel->lookup.results.hasNext);
    for (std::size_t i = 0; i < panel->rows.size(); ++i) {
        auto& row = panel->rows[i];
        const bool visible = i < panel->lookup.results.profiles.size() && !panel->lookup.loading;
        row.object->SetActive(visible);
        if (!visible) { ReleaseAvatar(row); continue; }
        const auto& profile = panel->lookup.results.profiles[i];
        row.text->set_text(StringW(profile.name + "\n" +
            (profile.country.empty() ? "Country unknown" : profile.country) + " | " +
            (profile.rank ? "Rank #" + std::to_string(profile.rank) : "Unranked") + "\nID: " + profile.id));
        panel->rowRevisions[i] = panel->lookup.revision;
    }
    RebuildSettings();
}

void SaveSelection(const std::shared_ptr<AccountPanel>& panel, const AccountProfile& profile) {
    if (!IsCanonicalAccountId(profile.id)) return;
    ++panel->metadataRevision;
    if (panel->service == AccountService::BeatLeader) getModConfig().BeatLeaderPlayerId.SetValue(profile.id);
    else getModConfig().ScoreSaberPlayerId.SetValue(profile.id);
    SaveName(panel->service, profile.name);
    panel->lookup.Invalidate();
    panel->open = false; panel->status.clear();
    UpdatePanel(panel);
}

void StartLookup(const std::shared_ptr<AccountPanel>& panel, int page = 1,
                 const std::string& directId = "") {
    if (!panel->selectedText) return;
    const auto request = panel->lookup.Begin();
    const auto input = directId.empty() ? panel->input : directId;
    const bool useSignedIn = !directId.empty();
    panel->open = true;
    panel->status = useSignedIn ? "Checking signed-in BeatLeader account..." : "Finding accounts...";
    UpdatePanel(panel);
    try {
        std::thread([panel, request, input, page, useSignedIn] {
            AccountPage result;
            std::string error;
            bool success = false;
            try { success = FindAccounts(panel->service, input, page, result, error); }
            catch (const std::exception& e) { error = std::string("Account lookup failed: ") + e.what(); }
            catch (...) { error = "Account lookup failed."; }
            auto profiles = result.profiles;
            BSML::MainThreadScheduler::Schedule([panel, request, result = std::move(result), error, success, useSignedIn]() mutable {
                if (!panel->selectedText || !panel->lookup.Complete(request, std::move(result))) return;
                if (success && useSignedIn && panel->lookup.results.profiles.size() == 1 &&
                    panel->selectedText->get_gameObject()->get_activeInHierarchy()) {
                    auto profile = panel->lookup.results.profiles.front();
                    SaveSelection(panel, profile); return;
                }
                panel->status = !success ? error : panel->lookup.results.profiles.empty() ? "No matching accounts found." :
                    "Page " + std::to_string(panel->lookup.results.page) + ": select your account below.";
                UpdatePanel(panel);
            });
            // Results become selectable before optional images are fetched.
            if (!success || useSignedIn) return;
            for (std::size_t i = 0; i < profiles.size(); ++i) {
                std::vector<std::uint8_t> bytes;
                try { if (!FetchAccountAvatar(profiles[i].avatar, bytes)) continue; }
                catch (...) { continue; }
                BSML::MainThreadScheduler::Schedule([panel, request, i, id = profiles[i].id, bytes = std::move(bytes)] {
                    auto* profile = panel->lookup.Select(request, i);
                    if (!panel->selectedText || !profile || profile->id != id || !panel->rows[i].image) return;
                    auto& row = panel->rows[i];
                    ReleaseAvatar(row);
                    row.sprite = BSML::Lite::VectorToSprite(bytes);
                    if (row.sprite) {
                        row.texture = row.sprite->get_texture();
                        row.image->set_sprite(row.sprite.ptr());
                        row.image->set_enabled(true);
                    }
                });
            }
        }).detach();
    } catch (const std::exception& e) {
        panel->lookup.Complete(request, {});
        panel->status = std::string("Could not start account lookup: ") + e.what(); UpdatePanel(panel);
    }
}

void UseBeatLeaderAccount(const std::shared_ptr<AccountPanel>& panel) {
    std::string error;
    CModInfo info{"BeatLeader", "", 0};
    if (!modloader_get_mod(&info, MatchType_IdOnly).handle) error = "Install and sign into BeatLeader first, or search for your account.";
    else try {
        // Same typed optional export used by BeatLeader's public API header.
        // No access to its Player object, credentials, or private globals.
        static auto getter = CondDeps::Find<std::optional<std::string>>("bl", "LoggedInPlayerId");
        if (!getter) error = "This BeatLeader version does not expose its account ID. Use account search instead.";
        else {
            auto id = getter.value()();
            if (id && IsCanonicalAccountId(*id)) { StartLookup(panel, 1, *id); return; }
            error = "Sign into BeatLeader first, or search for your account.";
        }
    } catch (...) { error = "BeatLeader's signed-in account could not be read. Use account search instead."; }
    panel->lookup.Invalidate(); panel->open = true; panel->status = error; UpdatePanel(panel);
}

HMUI::CurvedTextMeshPro* AccountText(UnityEngine::Transform* parent, const std::string& label, float size = 2.7f) {
    auto* text = BSML::Lite::CreateText(parent, StringW(label), TMPro::FontStyles::Normal, size);
    text->set_enableWordWrapping(true); text->set_richText(false);
    text->set_alignment(TMPro::TextAlignmentOptions::TopLeft);
    auto* layout = text->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>();
    layout->set_minWidth(0); layout->set_preferredWidth(0); layout->set_flexibleWidth(1);
    layout->set_minHeight(-1); layout->set_preferredHeight(-1); layout->set_flexibleHeight(0);
    return text;
}

template<class Layout> void AccountLayout(Layout* layout) {
    auto* fitter = layout->get_gameObject()->template GetComponent<UnityEngine::UI::ContentSizeFitter*>();
    fitter->set_horizontalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    layout->set_childControlWidth(true); layout->set_childControlHeight(true);
    layout->set_childForceExpandWidth(true); layout->set_childForceExpandHeight(false);
    layout->set_childAlignment(UnityEngine::TextAnchor::UpperLeft); layout->set_spacing(1.5f);
    auto* element = layout->get_gameObject()->template GetComponent<UnityEngine::UI::LayoutElement*>();
    element->set_minWidth(0); element->set_preferredWidth(0); element->set_flexibleWidth(1);
    element->set_minHeight(-1); element->set_preferredHeight(-1); element->set_flexibleHeight(0);
}

UnityEngine::UI::Button* AccountButton(UnityEngine::Transform* parent, const char* label, std::function<void()> click) {
    auto* button = BSML::Lite::CreateUIButton(parent, label, std::move(click));
    auto* fitter = button->get_gameObject()->GetComponent<UnityEngine::UI::ContentSizeFitter*>();
    fitter->set_horizontalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    auto* element = button->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>();
    element->set_minWidth(0); element->set_preferredWidth(0); element->set_flexibleWidth(1);
    return button;
}

void BuildPanel(UnityEngine::Transform* parent, AccountService service) {
    auto panel = std::make_shared<AccountPanel>(); panel->service = service;
    panels[service == AccountService::BeatLeader ? 0 : 1] = panel;
    panel->selectedText = AccountText(parent, "", 3.0f);
    auto* actions = BSML::Lite::CreateHorizontalLayoutGroup(parent); AccountLayout(actions);
    AccountButton(actions->get_transform(), "Choose account", [panel] {
        panel->lookup.Invalidate(); panel->open = !panel->open;
        panel->status = "Search by name, or enter a profile link or numeric ID."; UpdatePanel(panel);
    });
    AccountButton(actions->get_transform(), "Show / hide ID", [panel] { panel->showId = !panel->showId; UpdatePanel(panel); });
    if (service == AccountService::BeatLeader)
        panel->signedIn = AccountButton(parent, "Use signed-in BeatLeader account", [panel] { UseBeatLeaderAccount(panel); });
    auto* picker = BSML::Lite::CreateVerticalLayoutGroup(parent); AccountLayout(picker);
    panel->picker = picker->get_gameObject();
    auto* field = BSML::Lite::CreateStringSetting(picker->get_transform(), "Name / profile URL / ID", "", [panel](StringW value) {
        panel->input = static_cast<std::string>(value);
        panel->lookup.Invalidate(); panel->status = "Press Find account to search."; UpdatePanel(panel);
    });
    BSML::Lite::AddHoverHint(field, "Enter your display name, a /u/ profile link, or the existing numeric player ID.");
    auto* findActions = BSML::Lite::CreateHorizontalLayoutGroup(picker->get_transform()); AccountLayout(findActions);
    panel->find = AccountButton(findActions->get_transform(), "Find account", [panel] { StartLookup(panel); });
    AccountButton(findActions->get_transform(), "Cancel", [panel] {
        panel->lookup.Invalidate(); panel->open = false; UpdatePanel(panel);
    });
    AccountButton(picker->get_transform(), "Clear saved account", [panel] {
        ++panel->metadataRevision;
        if (panel->service == AccountService::BeatLeader) getModConfig().BeatLeaderPlayerId.SetValue("");
        else getModConfig().ScoreSaberPlayerId.SetValue("");
        SaveName(panel->service, "");
        panel->lookup.Invalidate(); panel->open = false; UpdatePanel(panel);
    });
    panel->statusText = AccountText(picker->get_transform(), "");
    for (std::size_t i = 0; i < panel->rows.size(); ++i) {
        auto* row = BSML::Lite::CreateHorizontalLayoutGroup(picker->get_transform()); AccountLayout(row);
        auto& entry = panel->rows[i]; entry.object = row->get_gameObject();
        entry.image = BSML::Lite::CreateImage(row->get_transform(), nullptr, {}, {10, 10});
        entry.image->set_preserveAspect(true); entry.image->set_raycastTarget(false);
        entry.image->set_enabled(false);
        auto* imageLayout = entry.image->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>();
        imageLayout->set_minWidth(10); imageLayout->set_preferredWidth(10); imageLayout->set_flexibleWidth(0);
        imageLayout->set_minHeight(10); imageLayout->set_preferredHeight(10); imageLayout->set_flexibleHeight(0);
        entry.text = AccountText(row->get_transform(), "");
        auto* select = AccountButton(row->get_transform(), "Select", [panel, i] {
            auto* found = panel->lookup.Select(panel->rowRevisions[i], i);
            if (!found) return;
            auto profile = *found; SaveSelection(panel, profile);
        });
        auto* buttonLayout = select->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>();
        buttonLayout->set_preferredWidth(18); buttonLayout->set_flexibleWidth(0);
    }
    auto* pages = BSML::Lite::CreateHorizontalLayoutGroup(picker->get_transform()); AccountLayout(pages);
    panel->previous = AccountButton(pages->get_transform(), "Previous page", [panel] { StartLookup(panel, panel->lookup.results.page - 1); });
    panel->next = AccountButton(pages->get_transform(), "Next page", [panel] { StartLookup(panel, panel->lookup.results.page + 1); });
    UpdatePanel(panel);
}

} // namespace

void BuildAccountSettings(UnityEngine::Transform* parent) {
    for (auto& old : panels) {
        if (!old) continue;
        old->lookup.Invalidate(); ++old->metadataRevision;
        for (auto& row : old->rows) ReleaseAvatar(row);
    }
    auto* layout = parent->get_gameObject()->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>();
    layout->set_childControlWidth(true); layout->set_childControlHeight(true);
    layout->set_childForceExpandWidth(true); layout->set_childForceExpandHeight(false);
    layout->set_childAlignment(UnityEngine::TextAnchor::UpperLeft); layout->set_spacing(2);
    auto wrapper = parent->get_parent()->get_gameObject();
    wrapper->GetComponent<UnityEngine::UI::ContentSizeFitter*>()->set_horizontalFit(
        UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    wrapper->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()->set_childForceExpandWidth(true);
    settingsContentRect = wrapper->GetComponent<UnityEngine::RectTransform*>();
    settingsScrollContent = wrapper->GetComponent<BSML::ScrollViewContent*>();
    BuildPanel(parent, AccountService::BeatLeader);
    BuildPanel(parent, AccountService::ScoreSaber);
    AccountText(parent, "Changes apply on the next playlist refresh. When viewing someone else's profile, turn off Record local attempts.", 2.5f);
    RefreshAccountSettings();
}

void RefreshAccountSettings() {
    for (auto& panel : panels) {
        if (!panel || !panel->selectedText) continue;
        panel->lookup.Invalidate(); panel->open = false; UpdatePanel(panel);
        const auto id = SavedId(panel->service);
        const auto revision = ++panel->metadataRevision;
        if (!IsCanonicalAccountId(id)) continue;
        try {
            std::thread([panel, id, revision] {
                AccountPage result; std::string error;
                try { if (!FindAccounts(panel->service, id, 1, result, error) || result.profiles.size() != 1) return; }
                catch (...) { return; }
                BSML::MainThreadScheduler::Schedule([panel, id, revision, name = result.profiles.front().name] {
                    if (!panel->selectedText || panel->metadataRevision != revision || SavedId(panel->service) != id) return;
                    SaveName(panel->service, name); UpdatePanel(panel);
                });
            }).detach();
        } catch (...) { /* Saved IDs remain usable when a lookup cannot start. */ }
    }
}

} // namespace rankedpractice
