// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Touch: taps, drags and flicks on every screen, the feedback of a finger and the rumble.
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

namespace {

// Touch: how far a finger moves before a tap turns into a drag.
constexpr float kDragSlop = 14;

} // namespace

void App::ReadTouch(u64& down) {
    HidTouchScreenState state{};
    perf::ReadTouch(state);
    if (m_wake_touch) {
        // the finger that woke the screen: nothing until it is lifted
        if (state.count == 0) m_wake_touch = false;
        return;
    }
    if (state.count > 0) {
        const float x = state.touches[0].x, y = state.touches[0].y;
        if (!m_touching) {
            m_touching = true;
            m_dragging = false;
            m_swipe = false;
            m_swipe_dx = 0;
            m_long_fired = false;
            m_touch_x0 = m_touch_x_prev = x;
            m_touch_y0 = m_touch_y_prev = y;
            m_touch_t_prev = m_time;
            m_drag_v = m_drag_vx = 0;
            m_fling_v = 0;      // a finger stops the grid
            m_list_fling = 0;
            m_touch_at = m_time;
            m_drag_scroll0 = m_scroll;
            // a list drawn last frame under the finger: dragged, it moves its focus row by row
            m_list_drag = m_list_pitch > 0 && m_dialog.kind == DialogKind::None && !m_menu && !m_act && !m_wifi_open && m_quick < 0 &&
                          m_list_area.Contains(x, y);
            m_drag_steps = 0;
            // the A-Z bar on the right edge: tapped or dragged along, it jumps to the letter
            m_az_touch = AzBarShown() && m_dialog.kind == DialogKind::None && !m_menu && !m_act && m_quick < 0 && x >= g_az_x - 22 &&
                         x < g_az_x + kAzW + 12 && y >= kAzTop - 8 && y < kAzTop + g_az_height + 8;
            // what is under the finger reacts at once (15A)
            m_press_live = false;
            m_press_cover = -1;
            if (!m_az_touch) {
                for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
                    if (it->rect.Contains(x, y)) {
                        m_press_rect = it->rect;
                        m_press_kind = it->kind;
                        m_press_cover = it->kind == HitKind::Cover ? it->index : -1;
                        m_press_live = true;
                        m_press_at = m_time;
                        break;
                    }
                }
            }
        }
        // the speed of the finger, smoothed (16A)
        const double dt = m_time - m_touch_t_prev;
        if (dt > 0.001) {
            const float vy = float((y - m_touch_y_prev) / dt), vx = float((x - m_touch_x_prev) / dt);
            m_drag_v = m_drag_v * 0.4f + vy * 0.6f;
            m_drag_vx = m_drag_vx * 0.4f + vx * 0.6f;
            m_touch_y_prev = y;
            m_touch_x_prev = x;
            m_touch_t_prev = m_time;
        }
        m_touch_x = x;
        m_touch_y = y;
        // held still for a moment: a long press (the menu of a file, as − does)
        if (!m_long_fired && !m_dragging && m_tab == Tab::More && m_more == MoreScreen::Files && m_time - m_touch_at > 0.55 && std::fabs(x - m_touch_x0) <= kDragSlop &&
            std::fabs(y - m_touch_y0) <= kDragSlop && m_dialog.kind == DialogKind::None) {
            m_long_fired = true;
            m_press_live = false; // the menu opens: the wave under the finger goes
            m_release_at = m_time;
            LongPress(x, y);
            if (m_files_menu) TouchTick();
        }
        if (m_az_touch) {
            const int slot = std::clamp(int((y - kAzTop) / (g_az_height / 27)), 0, 26);
            JumpToLetter(slot, 0);
            return;
        }
        const float dx = x - m_touch_x0, dy = y - m_touch_y0;
        const bool moved = std::fabs(dx) > kDragSlop || std::fabs(dy) > kDragSlop;
        if (moved && m_press_live) {
            m_press_live = false; // a drag, not a press: the wave goes
            m_release_at = m_time;
        }
        // held still on a cover: its quick menu (18A)
        if (!moved && !m_long_fired && m_press_cover >= 0 && m_press_kind == HitKind::Cover && GridTab() && !m_detail &&
            m_dialog.kind == DialogKind::None && !m_menu && !m_act && m_quick < 0 && m_time - m_press_at >= 0.45) {
            m_long_fired = true;
            m_press_live = false;
            m_release_at = m_time;
            OpenQuick(m_press_cover);
            TouchTick();
            return;
        }
        const bool in_grid = GridTab() && !m_detail && m_touch_y0 >= kGridTop - 10 && m_touch_y0 < kGridTop + g_view_h &&
                             m_touch_x0 < g_az_x - 22;
        if (!m_dragging && !m_swipe && moved && !m_long_fired) {
            // sideways: the tab beside, or back from a page (20A); up and down: the grid or the list
            const bool calm = !m_wizard && m_dialog.kind == DialogKind::None && !m_menu && !m_act && m_quick < 0 && !m_wifi_open;
            if (std::fabs(dx) > std::fabs(dy) * 1.2f && calm) {
                m_swipe = true;
            } else if (std::fabs(dy) > kDragSlop && (in_grid || m_list_drag)) {
                m_dragging = true;
                if (m_list_drag) ListDragStarts();
            }
        }
        if (m_swipe) {
            // the content follows the finger; from a page only to the right (back), and with resistance
            const bool sub = InSubScreen();
            float follow = dx;
            if (sub && follow < 0) follow *= 0.15f;
            m_swipe_dx = ui::Still() ? 0.f : follow * (sub ? 0.6f : 0.45f);
        } else if (m_dragging && m_list_drag) {
            // a row dragged up shows the next one: the focus goes down by one, as with the pad
            const int steps = int((m_touch_y0 - y) / m_list_pitch);
            m_drag_pending += steps - m_drag_steps;
            m_drag_steps = steps;
        } else if (m_dragging) {
            // the grid follows the finger; past its ends it gives a little (16A), at the top that
            // is pulling to read the library again (21A)
            const float raw = m_drag_scroll0 - dy, top = m_refreshing ? -46.f : 0.f, max = MaxScroll();
            float at = raw;
            if (ui::Still()) at = std::clamp(raw, 0.f, max);
            else if (raw < top) at = top + (raw - top) * 0.42f;
            else if (raw > max) at = max + (raw - max) * 0.42f;
            m_scroll = m_scroll_target = at;
            m_scroll_seen_at = m_time;
        }
    } else if (m_touching && m_az_touch) {
        m_touching = m_az_touch = false;
    } else if (m_touching) {
        m_touching = false;
        const float dx = m_touch_x - m_touch_x0;
        const bool moved = std::fabs(dx) > kDragSlop || std::fabs(m_touch_y - m_touch_y0) > kDragSlop;
        if (m_press_live) {
            m_press_live = false;
            m_release_at = m_time;
        }
        if (m_swipe) {
            // far enough, or quick enough: the tab beside (or back); else it springs back
            const bool far = std::fabs(dx) > 150 || (std::fabs(m_drag_vx) > 700 && std::fabs(dx) > 40);
            const bool sub = InSubScreen();
            if (far && sub && dx > 0) {
                down |= HidNpadButton_B;
                TouchTick();
                m_swipe_dx = 0;
            } else if (far && !sub) {
                const int dir = dx < 0 ? 1 : -1;
                m_tab = Tab((int(m_tab) + dir + kTabs) % kTabs);
                // the new tab comes in from where the finger left the old one
                m_tab_from_next = float(dir) * std::max(80.f, 380.f - std::fabs(m_swipe_dx));
                m_swipe_dx = 0;
                TouchTick();
            } else {
                m_swipe_back = m_swipe_dx;
                m_swipe_back_at = m_time;
                m_swipe_dx = 0;
            }
            m_swipe = false;
        } else if (m_dragging && m_list_drag) {
            // let go with speed: the list goes on a few rows, slowing down (16A)
            if (!ui::Still() && std::fabs(m_drag_v) > 400 && m_list_pitch > 0) {
                m_list_fling = -m_drag_v;
                m_list_fling_d = 0;
                m_list_fling_pitch = m_list_pitch;
            }
        } else if (m_dragging) {
            if (m_scroll < -kPullArm && m_tab == Tab::Library && !m_shop_active && !m_refreshing &&
                m_catalog.GetState() != Catalog::State::Loading) {
                // pulled far enough: the library is read again (21A)
                const Config c = m_link.CurrentConfig();
                if (c.Linked()) {
                    m_refreshing = true;
                    m_refresh_at = m_time;
                    m_catalog.Load(c.url, c.key);
                    TouchTick();
                }
            }
            if (!ui::Still() && std::fabs(m_drag_v) > 250) {
                m_fling_v = std::clamp(-m_drag_v, -4200.f, 4200.f);
                m_fling_end_told = false;
            }
            m_scroll_target = std::clamp(m_scroll, m_refreshing ? -46.f : 0.f, MaxScroll());
        } else if (!moved && !m_long_fired) {
            if (Tap(m_touch_x, m_touch_y, down)) TouchTick();
        }
        m_dragging = false;
        m_list_drag = false;
    }
}

void App::ListDragStarts() {
    // the screens with a side list too: the one dragged takes the focus
    if (m_tab != Tab::More) return;
    if (m_more == MoreScreen::Files) m_files_side = false;
    if (m_more == MoreScreen::Saves) m_vault_side = 0;
}

bool App::Tap(float x, float y, u64& down) {
    if (m_wifi_open) {
        // the WiFi card is on top: a tap on it does nothing, anywhere else closes it
        if (!m_wifi_card.Contains(x, y)) m_wifi_open = false;
        return true;
    }
    // Last drawn on top wins: walk backwards.
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (!it->rect.Contains(x, y)) {
            continue;
        }
        switch (it->kind) {
            case HitKind::Button: down |= it->rect.button; break;
            case HitKind::Tab: m_tab = Tab(it->index); break;
            case HitKind::Wifi: m_wifi_open = true; break;
            case HitKind::BuildNote:
                // "Unofficial" in the top bar: what it means, in Settings › Help › Code and license
                m_tab = Tab::Settings;
                m_setting = kRowLicense;
                break;
            case HitKind::Chip:
                if (m_filter != it->index) m_wave_at = m_time; // the covers come in as a wave (9A)
                m_filter = it->index;
                RebuildView();
                break;
            case HitKind::SearchChip:
                // the chip of the search: a tap clears it
                m_search.clear();
                RebuildView();
                break;
            case HitKind::Cover:
                // First tap marks it, a second tap opens it (as A does).
                if (m_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_focus = it->index;
                    KeepFocusVisible();
                    // marked by a finger: a bubble on it opens it (19A)
                    m_touch_focus = true;
                    m_touch_focus_at = m_time;
                }
                break;
            case HitKind::QueueStrip: m_tab = Tab::Queue; break;
            case HitKind::SettingsRow:
                // as in the library: the first tap marks a row, a second one uses it
                if (m_setting == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_setting = it->index;
                }
                break;
            case HitKind::StyleCard:
                // a card of a style is chosen at once, its wave from the finger
                m_setting = kRowStyle;
                SetStyle(it->index, m_touch_x, m_touch_y);
                break;
            case HitKind::PairRow:
                if (m_pair_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_pair_focus = it->index;
                }
                break;
            case HitKind::Remember: down |= HidNpadButton_Y; break;
            case HitKind::Install:
                // the button acts at once, as a button of a question does
                m_detail_focus = it->index;
                down |= HidNpadButton_A;
                break;
            case HitKind::DetailRow:
                if (m_detail_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_detail_focus = it->index;
                }
                break;
            case HitKind::Dest:
                m_to_nand = it->index == 1;
                m_dest_touched = true;
                break;
            case HitKind::FilesDest:
                m_files_nand = it->index == 1;
                break;
            case HitKind::NoRoomUse: UseNandOnce(it->index); break; // 12A

            case HitKind::QueueRow:
                if (m_queue_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_queue_focus = it->index;
                }
                break;
            case HitKind::InstalledView:
                if (m_installed_view != it->index) {
                    m_installed_view = it->index;
                    m_installed_focus = 0;
                    m_console_app = 0;
                }
                break;
            case HitKind::ShopChip:
                if (it->index == int(m_shops.size())) {
                    // a shop more: the list, where it is added
                    LeaveShop();
                    m_shop_cache.clear();
                    m_shop_cached = -1;
                    m_shop_focus = int(m_shops.size());
                } else if (it->index != m_shop_opening) {
                    OpenShopAt(it->index);
                }
                break;
            case HitKind::ConsoleSize:
                if (m_ui.console_size != it->index) {
                    m_ui.console_size = it->index;
                    m_ui.Save();
                }
                break;
            case HitKind::InstalledRow:
                if (m_installed_view == 0) {
                    // In the console: a game of the list, or a line of its page
                    int& focus = m_console_app ? m_console_item : m_console_focus;
                    if (m_console_app && m_console_btn) {
                        m_console_btn = 0; // from Play or Repair to a row
                        focus = it->index;
                        break;
                    }
                    if (focus == it->index) {
                        down |= HidNpadButton_A;
                    } else {
                        focus = it->index;
                    }
                    break;
                }
                if (m_installed_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_installed_focus = it->index;
                }
                break;
            case HitKind::WizItem: {
                // as everywhere: the first tap marks it, a second one uses it
                const int group = it->index / 100, item = it->index % 100;
                if (m_wiz_group == group && m_wiz_item == item) {
                    ActivateWizard(group, item);
                } else {
                    m_wiz_group = group;
                    m_wiz_item = item;
                }
                break;
            }
            case HitKind::WizButton:
                // Back and Next act at once, as the buttons of a question
                m_wiz_group = int(WizardGroups().size()) - 1;
                m_wiz_item = it->index;
                ActivateWizard(m_wiz_group, it->index);
                break;
            case HitKind::WizMusic: down |= HidNpadButton_Minus; break;
            case HitKind::MenuBack: m_menu = false; break;
            case HitKind::ActBack: m_act = false; break;
            case HitKind::ActRow: {
                // a row acts at once, as a button of a question does
                std::string title;
                const auto rows = ActRows(m_act_screen, title);
                if (it->index >= 0 && it->index < int(rows.size()) && rows[size_t(it->index)].enabled) {
                    m_act_focus = it->index;
                    ActDo(rows[size_t(it->index)]);
                }
                break;
            }
            case HitKind::RemoveStop: StopRemove(); break;
            case HitKind::DialogTick:
                m_dialog.keep_saves = !m_dialog.keep_saves;
                m_dialog.tick_focus = 0;
                break;
            case HitKind::DialogTickExtra:
                m_dialog.remove_extra = !m_dialog.remove_extra;
                m_dialog.tick_focus = 1;
                break;
            case HitKind::ConsoleButton:
                // Play or Repair on the page of a game: they act at once, as buttons do
                m_console_btn = it->index;
                down |= HidNpadButton_A;
                break;
            case HitKind::ConsoleOrphans:
                m_console_orphans = !m_console_orphans;
                m_console_missing = false;
                m_console_focus = 0;
                m_console_list_at = m_time;
                break;
            case HitKind::CardRow:
                // a content of the game card (14A): a second tap ticks it
                if (m_card_focus == it->index) down |= HidNpadButton_A;
                else m_card_focus = it->index;
                break;
            case HitKind::CardInstall:
                m_card_focus = it->index;
                down |= HidNpadButton_A;
                break;
            case HitKind::CardDest: m_card_nand = it->index == 1; break;
            case HitKind::ViewerArea: break; // the text viewer: a finger drags it, a tap does nothing
            case HitKind::TicketRow:
                if (m_ticket_focus == it->index) {
                    down |= HidNpadButton_Y;
                } else {
                    m_ticket_focus = it->index;
                }
                break;
            case HitKind::MenuRow:
                // a row of the menu acts at once, as a button of a question does
                m_menu_focus = it->index;
                ActivateMenu(it->index);
                break;
            case HitKind::MoreTile:
                if (m_tile == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_tile = it->index;
                }
                break;
            case HitKind::MorePlace:
                m_files_side = true;
                if (m_place == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_place = it->index;
                }
                break;
            case HitKind::MoreRow:
                m_files_side = false;
                if (m_file_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_file_focus = it->index;
                }
                break;
            case HitKind::MoreMark:
                m_files_side = false;
                m_file_focus = it->index;
                down |= HidNpadButton_Y;
                break;
            case HitKind::MoreSide:
                break;
            case HitKind::FilesMenuRow:
                // a row of the menu acts at once, as a button of a question does
                m_files_menu_focus = it->index;
                FilesDo(it->index);
                break;
            case HitKind::FilesMenuBack:
                m_files_menu = false;
                m_vault_menu = false;
                break;
            case HitKind::VaultKind:
                if (m_vault_kind != it->index) {
                    m_vault_kind = it->index;
                    OpenVault();
                }
                break;
            case HitKind::VaultMenuRow:
                m_vault_menu_focus = it->index;
                VaultMenuDo(it->index);
                break;
            case HitKind::MtpUnit:
                m_mtp_unit = it->index;
                ToggleMtpUnit(it->index);
                break;
            case HitKind::UsbChip:
                // the chip of USB 3.0 opens its sheet at once (USB A)
                if (m_more == MoreScreen::Mtp) m_mtp_unit = -1;
                else m_usb_chip_focus = true;
                OpenUsb3();
                break;
            case HitKind::ConsoleMissing:
                m_console_missing = !m_console_missing;
                m_console_orphans = false;
                m_console_focus = 0;
                m_console_list_at = m_time;
                break;
            case HitKind::FilesView:
                if (m_files_all != (it->index == 1)) FilesDo(kFaView);
                break;
            case HitKind::FilesAfter:
                m_files_after = !m_files_after;
                break;
            case HitKind::ModRow: {
                const int side = it->index >= 100 ? 1 : 0, row = it->index >= 100 ? it->index - 100 : 0;
                if (m_mods_side == side && m_mods_focus == row) {
                    down |= HidNpadButton_A;
                } else {
                    m_mods_side = side;
                    m_mods_focus = row;
                }
                break;
            }
            case HitKind::ShopRow:
                if (m_shop_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_shop_focus = it->index;
                }
                break;
            case HitKind::CompressRow:
                if (m_compress_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_compress_focus = it->index;
                }
                break;
            case HitKind::CompressMark:
                // the box of a row (1A): it marks, as Y does
                m_compress_focus = it->index;
                m_compress_app = 0; // the focus follows the row tapped
                down |= HidNpadButton_Y;
                break;
            case HitKind::CompressFilter:
                m_compress_all = !m_compress_all; // every game, or only those that can be compressed (4A)
                break;
            case HitKind::VersionChip:
                down |= HidNpadButton_Minus;
                break;
            case HitKind::SdChoice:
                if (it->index >= 2 || m_sd_choice == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_sd_choice = it->index;
                }
                break;
            case HitKind::SpaceAction:
                if (m_space_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_space_focus = it->index;
                }
                break;
            case HitKind::PickRow:
                if (m_more == MoreScreen::Mods) {
                    if (m_mods_pick == it->index) down |= HidNpadButton_A;
                    else m_mods_pick = it->index;
                    break;
                }
                if (m_pick_focus == it->index) {
                    down |= m_pick_kind == 0 ? HidNpadButton_Y : HidNpadButton_A;
                } else {
                    m_pick_focus = it->index;
                }
                break;
            case HitKind::VaultGame:
                m_vault_side = 0;
                if (m_vault_focus != it->index) m_vault_copy = 0;
                m_vault_focus = it->index;
                break;
            case HitKind::VaultCopy:
                if (m_vault_side == 1 && m_vault_copy == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_vault_side = 1;
                    m_vault_copy = it->index;
                }
                break;
            case HitKind::DialogBack: down |= HidNpadButton_B; break; // as B: each question answers its own way
            case HitKind::DialogLine:
                if (m_dialog.kind == DialogKind::UpdateHere) {
                    m_dialog.entry = it->index;
                    if (it->index >= 0 && it->index < int(m_dialog.marks.size())) m_dialog.marks[size_t(it->index)] ^= 1;
                    break;
                }
                if (m_dialog.entry == it->index) {
                    m_dialog.focus = 1;
                    down |= HidNpadButton_A;
                } else {
                    m_dialog.entry = it->index;
                }
                break;
            case HitKind::DialogButton:
                // a button of the question acts at once: tapping it is the choice
                m_dialog.focus = it->index;
                down |= HidNpadButton_A;
                break;
            case HitKind::OpenBubble: down |= HidNpadButton_A; break; // the bubble on a cover tapped once (19A)
            case HitKind::QuickRow: ActivateQuick(it->index); break;    // a row of the quick menu acts at once (18A)
            case HitKind::QuickBack: m_quick = -1; break;
        }
        return true;
    }
    return false;
}

void App::InitRumble() {
    // the console in the hands, and player 1's controllers (two Joy-Con or a Pro Controller)
    m_vib_ok[0] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_handheld, 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld));
    m_vib_ok[1] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_dual, 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual));
    m_vib_ok[2] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_full, 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey));
}

void App::SendRumble(bool on, float strength) {
    // short and gentle: a quarter of the strength, at the frequencies the console uses for a tap
    HidVibrationValue v[2];
    for (auto& value : v) {
        value.amp_low = on ? 0.25f * strength : 0.f;
        value.freq_low = 160.f;
        value.amp_high = on ? 0.20f * strength : 0.f;
        value.freq_high = 320.f;
    }
    if (m_vib_ok[0] && (hidGetNpadStyleSet(HidNpadIdType_Handheld) & HidNpadStyleTag_NpadHandheld)) {
        hidSendVibrationValues(m_vib_handheld, v, 2);
    }
    const u32 style = hidGetNpadStyleSet(HidNpadIdType_No1);
    if (m_vib_ok[1] && (style & HidNpadStyleTag_NpadJoyDual)) {
        hidSendVibrationValues(m_vib_dual, v, 2);
    }
    if (m_vib_ok[2] && (style & HidNpadStyleTag_NpadFullKey)) {
        hidSendVibrationValues(m_vib_full, v, 1);
    }
}

void App::TouchTick() {
    // a tap of 18 ms at less than half the strength of the end of a queue (17A)
    if (!m_ui.touch_rumble || m_rumble_step >= 0) return;
    SendRumble(true, 0.45f);
    m_tick_off_at = m_time + 0.018;
}

void App::DrawTouchFeedback(NVGcontext* vg) {
    // 15A: what the finger presses darkens a little and a soft wave grows out of the finger; let go,
    // the wave fades. With the animations off, it is lit while pressed, without the wave.
    const Theme& t = m_theme;
    if (m_az_touch || m_swipe || m_dragging) return;
    const bool held = m_touching && m_press_live;
    const double since = m_time - m_release_at;
    if (m_press_at < 0 || (!held && (m_release_at < m_press_at || since > 0.35))) {
        // nothing pressed, or its wave is gone
    } else {
        ui::HitRect r = m_press_rect;
        float radius = 10;
        if (m_press_kind == HitKind::Cover) {
            // the cover itself, not the gap around it
            r.x += kGridGap / 2;
            r.w -= kGridGap;
            r.h = std::min(r.h, g_cover);
        } else if (m_press_kind == HitKind::Tab || m_press_kind == HitKind::Chip) {
            radius = r.h / 2;
        }
        const float fade = held ? 1.f : float(std::max(0.0, 1 - since / 0.35));
        nvgSave(vg);
        nvgIntersectScissor(vg, r.x, r.y, r.w, r.h);
        if (m_press_kind != HitKind::Cover) {
            ui::FillRound(vg, r.x, r.y, r.w, r.h, radius, nvgRGBAf(0, 0, 0, (t.dark ? 0.10f : 0.05f) * fade));
        }
        if (ui::Still()) {
            NVGcolor lit = t.work;
            lit.a = 0.18f * fade;
            ui::FillRound(vg, r.x, r.y, r.w, r.h, radius, lit);
        } else {
            const float far = std::hypot(std::max(m_touch_x0 - r.x, r.x + r.w - m_touch_x0), std::max(m_touch_y0 - r.y, r.y + r.h - m_touch_y0));
            const float grow = ui::EaseOut(float(std::min(1.0, (m_time - m_press_at) / 0.45)));
            NVGcolor wave = t.dark ? nvgRGBAf(1, 1, 1, 0.16f) : t.work;
            if (!t.dark) wave.a = 0.16f;
            wave.a *= fade;
            nvgBeginPath(vg);
            nvgCircle(vg, m_touch_x0, m_touch_y0, std::max(8.f, far * grow));
            nvgFillColor(vg, wave);
            nvgFill(vg);
        }
        nvgRestore(vg);
    }
    // 18A: held still on a cover, a ring fills around the finger until its quick menu opens; on a
    // file of More › Files, until its menu opens
    const bool cover_hold = m_press_kind == HitKind::Cover && GridTab() && !m_detail && m_quick < 0;
    const bool file_hold = (m_press_kind == HitKind::MoreRow || m_press_kind == HitKind::MoreMark) && m_tab == Tab::More &&
                           m_more == MoreScreen::Files && !m_files_menu && !m_fwork;
    if (m_touching && m_press_live && !m_long_fired && (cover_hold || file_hold)) {
        const double hold = cover_hold ? 0.45 : 0.55;
        const float p = float((m_time - m_press_at - 0.12) / (hold - 0.12));
        if (p > 0) {
            NVGcolor track = t.work;
            track.a = 0.25f;
            nvgBeginPath(vg);
            nvgCircle(vg, m_touch_x0, m_touch_y0, 38);
            nvgStrokeColor(vg, track);
            nvgStrokeWidth(vg, 6);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgArc(vg, m_touch_x0, m_touch_y0, 38, -NVG_PI / 2, -NVG_PI / 2 + 2 * NVG_PI * std::min(1.f, p), NVG_CW);
            nvgStrokeColor(vg, t.work);
            nvgLineCap(vg, NVG_ROUND);
            nvgStroke(vg);
        }
    }
}

} // namespace nxdock
