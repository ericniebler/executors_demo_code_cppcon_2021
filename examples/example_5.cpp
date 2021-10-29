/*
 * Copyright Eric Niebler.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unifex/done_as_optional.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/stop_if_requested.hpp>
#include <unifex/sequence.hpp>
#include <unifex/manual_event_loop.hpp>

#include <cassert>
#include <chrono>
#include <ranges>
#include <stop_token>

#include <conio.h>
#include <Windows.h>

static constexpr char CTRL_C = (char)0x03;

// A rough approximation of C++20's std::jthread,
// because MSVC doesn't have it yet.
struct jthread {
  std::stop_source stop_;
  std::thread thread_;

  template <class Fn, class... Args>
  jthread(Fn fn, Args... args)
    : stop_()
    , thread_(
          [token = stop_.get_token()](auto fn, auto... args) {
            fn(token, args...);
          },
          fn,
          args...) {}

  ~jthread() {
    stop_.request_stop();
    thread_.join();
  }
};

void register_keyboard_callback(void (*callback)(char)) {
  static jthread th([=](auto token) {
    while (!token.stop_requested()) {
      int ch = _getch();
      callback((char)ch);
      if (ch == CTRL_C)
        break;
    }
  });
}

// Implementation detail, the shape of which is likely to evolve.
template <class... Values>
struct _sender_of {
  template <template <class...> class Variant, template <class...> class Tuple>
  using value_types = Variant<Tuple<Values...>>;
  template <template <class...> class Variant>
  using error_types = Variant<std::exception_ptr>;
  static constexpr bool sends_done = true;
};

struct pending_completion {
  virtual void complete(char) = 0;
  virtual void cancel() = 0;
  virtual ~pending_completion() {}
};

std::atomic<pending_completion *> pending_completion_{nullptr};

static void on_keyclick(char ch) {
  auto* current = pending_completion_.exchange(nullptr);
  if (current != nullptr) {
    current->complete(ch);
  }
}

struct cancel_keyclick {
  void operator()() const noexcept {
    auto* current = pending_completion_.exchange(nullptr);
    if (current != nullptr) {
      current->cancel();
    }
  }
};

template <unifex::receiver Rec, std::invocable Fn>
using stop_callback_for_t =
  typename unifex::stop_token_type_t<Rec>::template callback_type<Fn>;

template <unifex::receiver_of<char> Rec>
struct keyclick_operation : pending_completion {
  using stop_callback_t = stop_callback_for_t<Rec, cancel_keyclick>;
  Rec rec_;
  std::optional<stop_callback_t> on_stop_{};

  explicit keyclick_operation(Rec rec) : rec_(std::move(rec)) {}

  void complete(char ch) override final {
    unifex::set_value(std::move(rec_), ch);
  }

  void cancel() override final {
    unifex::set_done(std::move(rec_));
  }

  void start() noexcept {
    // Register the stop callback
    on_stop_.emplace(unifex::get_stop_token(rec_), cancel_keyclick{});
    // Enqueue the operation
    auto* previous = pending_completion_.exchange(this);
    /// NB: There is a race condition between the above two
    /// lines in which a stop request can get dropped. See
    /// the kbrdhook example for one possible solution.
    assert(previous == nullptr);
  }
};

struct keyclick_sender : _sender_of<char> {
  auto connect(unifex::receiver_of<char> auto rec) {
    return keyclick_operation{std::move(rec)};
  }
};

keyclick_sender read_keyclick() {
  return {};
}

struct ctrl_c_handler {
  struct pending {
    virtual void complete() = 0;
    virtual ~pending() {}
  };
  static inline std::atomic<pending*> pending_{nullptr};

  static BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
      if (auto* pending = pending_.exchange(nullptr))
        pending->complete();
    }
    return TRUE;
  }

  ctrl_c_handler() {
    BOOL result = ::SetConsoleCtrlHandler(&consoleHandler,TRUE);
    assert(result);
  }
  ~ctrl_c_handler() {
    BOOL result = ::SetConsoleCtrlHandler(&consoleHandler,FALSE);
    assert(result);
  }
  ctrl_c_handler(ctrl_c_handler&&) = delete;

  [[nodiscard]] auto event() const;
};

auto ctrl_c_handler::event() const {
  return unifex::create_simple<>([]<unifex::receiver_of R>(R& rec) {
    struct state : pending {
      R& rec_;
      state(R& rec) : rec_(rec) {
        auto* previous = pending_.exchange(this);
        assert(previous == nullptr);
      }
      void complete() override final {
        unifex::set_value(std::move(rec_));
      }
      state(state&&) = delete;
    };
    return state{rec};
  });
}

auto keyclicks() {
  return std::views::iota(0u)
    | std::views::transform([](auto) { return read_keyclick(); });
}

unifex::task<void> echo_keyclicks() {
  for (auto keyclick :
       keyclicks() | std::views::transform(unifex::done_as_optional)) {
    std::optional<char> ch = co_await std::move(keyclick);

    if (ch) {
      printf("Read a character! %c\n", *ch);
    } else {
      printf("Interrupt!\n");
      break;
    }
  }
}

int main() {
  register_keyboard_callback(on_keyclick);
  ctrl_c_handler ctrl_c;

  (void) unifex::sync_wait(
      echo_keyclicks()
    | unifex::stop_when(ctrl_c.event()));
}
