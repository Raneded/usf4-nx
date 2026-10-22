/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/input/state_merge.h>
#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>

#include <mutex>
#include <set>
#include <string>
#include <tuple>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;

using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;

constexpr uint32_t XINPUT_FLAG_GAMEPAD = 0x01;
constexpr uint32_t XINPUT_FLAG_ANY_USER = 1 << 30;

rex::input::InputSystem* input_system() {
  return static_cast<rex::input::InputSystem*>(REX_KERNEL_STATE()->emulator()->input_system());
}

// usf4-nx: XUSER_INDEX_ANY (0xFF) or XINPUT_FLAG_ANY_USER asks about every player's controller (the title screen's
// "press Start" and the like). It used to be pinned to user 0, so a controller that was player 2 could never answer
// it. Keystrokes come from the first player that has one, stamped with that player; states are merged.
bool IsAnyUser(uint32_t user_index, uint32_t flags) {
  return (user_index & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER);
}

// The first call of each kind per user and flags, so a log shows which players the game asks about.
void LogFirstQuery(const char* what, uint32_t user_index, uint32_t flags) {
  static std::mutex mutex;
  static std::set<std::tuple<std::string, uint32_t, uint32_t>> seen;
  std::lock_guard<std::mutex> lock(mutex);
  if (seen.size() < 64 && seen.emplace(what, user_index, flags).second) {
    REXLOG_INFO("input: the game asks {} for user 0x{:X} (flags 0x{:X})", what, user_index, flags);
  }
}

X_RESULT GetKeystrokeAnyUser(uint32_t flags, X_INPUT_KEYSTROKE* keystroke) {
  bool connected = false;
  for (uint32_t user = 0; user < rex::input::kMaxGuestUsers; ++user) {
    const X_RESULT result = input_system()->GetKeystroke(user, flags, keystroke);
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
    connected |= result != X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT GetStateAnyUser(X_INPUT_STATE* out_state) {
  X_INPUT_STATE merged{};
  uint32_t packets = 0;
  bool any = false;
  for (uint32_t user = 0; user < rex::input::kMaxGuestUsers; ++user) {
    X_INPUT_STATE state{};
    if (input_system()->GetState(user, &state) != X_ERROR_SUCCESS) {
      continue;
    }
    // The packet number changes whenever any player's does (the players' own counters are not comparable).
    packets += static_cast<uint32_t>(state.packet_number);
    if (!any) {
      merged = state;
      any = true;
    } else {
      rex::input::MergeInto(merged, state);
    }
  }
  if (!any) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  merged.packet_number = packets;
  if (out_state) {
    *out_state = merged;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT GetCapabilitiesAnyUser(uint32_t flags, X_INPUT_CAPABILITIES* caps) {
  X_RESULT result = X_ERROR_DEVICE_NOT_CONNECTED;
  for (uint32_t user = 0; user < rex::input::kMaxGuestUsers; ++user) {
    result = input_system()->GetCapabilities(user, flags, caps);
    if (result == X_ERROR_SUCCESS) {
      break;
    }
  }
  return result;
}

void XamResetInactivity_entry() {
  // Do we need to do anything?
}

u32 XamEnableInactivityProcessing_entry(u32 unk, u32 enable) {
  return X_ERROR_SUCCESS;
}

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetcapabilities(v=vs.85).aspx
u32 XamInputGetCapabilities_entry(u32 user_index, u32 flags, ppc_ptr_t<X_INPUT_CAPABILITIES> caps) {
  REXKRNL_TRACE("[XAM] XamInputGetCapabilities called: user={}, flags=0x{:X}", (uint32_t)user_index,
                (uint32_t)flags);
  if (!caps) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  LogFirstQuery("capabilities", user_index, flags);
  if (IsAnyUser(user_index, flags)) {
    return GetCapabilitiesAnyUser(flags, caps);
  }
  auto* is = input_system();
  return is->GetCapabilities(user_index, flags, caps);
}

u32 XamInputGetCapabilitiesEx_entry(u32 unk, u32 user_index, u32 flags,
                                    ppc_ptr_t<X_INPUT_CAPABILITIES> caps) {
  if (!caps) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  LogFirstQuery("capabilities", user_index, flags);
  if (IsAnyUser(user_index, flags)) {
    return GetCapabilitiesAnyUser(flags, caps);
  }
  auto* is = input_system();
  return is->GetCapabilities(user_index, flags, caps);
}

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetstate(v=vs.85).aspx
u32 XamInputGetState_entry(u32 user_index, u32 flags, ppc_ptr_t<X_INPUT_STATE> input_state) {
  // Games call this with a NULL state ptr, probably as a query.
  static int call_count = 0;
  if (++call_count <= 5) {
    REXKRNL_TRACE("[XAM] XamInputGetState called: user={}, flags=0x{:X}", (uint32_t)user_index,
                  (uint32_t)flags);
  }

  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  LogFirstQuery("the state", user_index, flags);
  if (IsAnyUser(user_index, flags)) {
    return GetStateAnyUser(input_state);
  }
  auto* is = input_system();
  return is->GetState(user_index, input_state);
}

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputsetstate(v=vs.85).aspx
u32 XamInputSetState_entry(u32 user_index, u32 unk, ppc_ptr_t<X_INPUT_VIBRATION> vibration) {
  if (!vibration) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  uint32_t actual_user_index = user_index;
  if ((user_index & 0xFF) == 0xFF) {
    // Always pin user to 0.
    actual_user_index = 0;
  }

  (void)unk;  // Unused in this implementation
  auto* is = input_system();
  return is->SetState(actual_user_index, vibration);
}

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetkeystroke(v=vs.85).aspx
u32 XamInputGetKeystroke_entry(u32 user_index, u32 flags, ppc_ptr_t<X_INPUT_KEYSTROKE> keystroke) {
  // https://github.com/CodeAsm/ffplay360/blob/master/Common/AtgXime.cpp
  // user index = index or XUSER_INDEX_ANY
  // flags = XINPUT_FLAG_GAMEPAD (| _ANYUSER | _ANYDEVICE)

  if (!keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  LogFirstQuery("a keystroke", user_index, flags);
  if (IsAnyUser(user_index, flags)) {
    return GetKeystrokeAnyUser(flags, keystroke);
  }
  auto* is = input_system();
  return is->GetKeystroke(user_index, flags, keystroke);
}

// Same as non-ex, just takes a pointer to user index.
u32 XamInputGetKeystrokeEx_entry(mapped_u32 user_index_ptr, u32 flags,
                                 ppc_ptr_t<X_INPUT_KEYSTROKE> keystroke) {
  if (!keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  uint32_t user_index = *user_index_ptr;
  LogFirstQuery("a keystroke (Ex)", user_index, flags);
  auto result = IsAnyUser(user_index, flags) ? GetKeystrokeAnyUser(flags, keystroke)
                                             : input_system()->GetKeystroke(user_index, flags, keystroke);
  if (XSUCCEEDED(result)) {
    *user_index_ptr = keystroke->user_index;
  }
  return result;
}

i32 XamUserGetDeviceContext_entry(u32 user_index, u32 unk, mapped_u32 out_ptr) {
  // Games check the result - usually with some masking.
  // If this function fails they assume zero, so let's fail AND
  // set zero just to be safe.
  *out_ptr = 0;
  LogFirstQuery("the device context", user_index, unk);
  if (!user_index || (user_index & 0xFF) == 0xFF) {
    return X_E_SUCCESS;
  }
  // usf4-nx: players 2-4 have a controller when one is assigned to them (this used to fail for every user but 0, so
  // a game asking about player 2 was told the controller was gone).
  X_INPUT_CAPABILITIES caps{};
  return input_system()->GetCapabilities(user_index, 0, &caps) == X_ERROR_SUCCESS ? X_E_SUCCESS
                                                                                  : X_E_DEVICE_NOT_CONNECTED;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

REX_EXPORT(__imp__XamResetInactivity, rex::kernel::xam::XamResetInactivity_entry)
REX_EXPORT(__imp__XamEnableInactivityProcessing,
           rex::kernel::xam::XamEnableInactivityProcessing_entry)
REX_EXPORT(__imp__XamInputGetCapabilities, rex::kernel::xam::XamInputGetCapabilities_entry)
REX_EXPORT(__imp__XamInputGetCapabilitiesEx, rex::kernel::xam::XamInputGetCapabilitiesEx_entry)
REX_EXPORT(__imp__XamInputGetState, rex::kernel::xam::XamInputGetState_entry)
REX_EXPORT(__imp__XamInputSetState, rex::kernel::xam::XamInputSetState_entry)
REX_EXPORT(__imp__XamInputGetKeystroke, rex::kernel::xam::XamInputGetKeystroke_entry)
REX_EXPORT(__imp__XamInputGetKeystrokeEx, rex::kernel::xam::XamInputGetKeystrokeEx_entry)
REX_EXPORT(__imp__XamUserGetDeviceContext, rex::kernel::xam::XamUserGetDeviceContext_entry)

REX_EXPORT_STUB(__imp__XamInputControl);
REX_EXPORT_STUB(__imp__XamInputEnableAutobind);
REX_EXPORT_STUB(__imp__XamInputGetDeviceStats);
REX_EXPORT_STUB(__imp__XamInputGetFailedConnectionOrBind);
REX_EXPORT_STUB(__imp__XamInputGetKeyLocks);
REX_EXPORT_STUB(__imp__XamInputGetKeystrokeHud);
REX_EXPORT_STUB(__imp__XamInputGetKeystrokeHudEx);
REX_EXPORT_STUB(__imp__XamInputGetUserVibrationLevel);
REX_EXPORT_STUB(__imp__XamInputNonControllerGetRaw);
REX_EXPORT_STUB(__imp__XamInputNonControllerGetRawEx);
REX_EXPORT_STUB(__imp__XamInputNonControllerSetRaw);
REX_EXPORT_STUB(__imp__XamInputNonControllerSetRawEx);
REX_EXPORT_STUB(__imp__XamInputRawState);
REX_EXPORT_STUB(__imp__XamInputResetLayoutKeyboard);
REX_EXPORT_STUB(__imp__XamInputSendStayAliveRequest);
REX_EXPORT_STUB(__imp__XamInputSendXenonButtonPress);
REX_EXPORT_STUB(__imp__XamInputSetKeyLocks);
REX_EXPORT_STUB(__imp__XamInputSetKeyboardTranslationHud);
REX_EXPORT_STUB(__imp__XamInputSetLayoutKeyboard);
REX_EXPORT_STUB(__imp__XamInputSetMinMaxAuthDelay);
REX_EXPORT_STUB(__imp__XamInputSetTextMessengerIndicator);
REX_EXPORT_STUB(__imp__XamInputToggleKeyLocks);
