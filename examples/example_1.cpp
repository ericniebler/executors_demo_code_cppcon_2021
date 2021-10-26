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

#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>

#include <cassert>
#include <stop_token>

#include <conio.h>

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
  virtual ~pending_completion() {}
};

std::atomic<pending_completion *> pending_completion_{nullptr};

static void on_keyclick(char ch) {
  auto* current = pending_completion_.exchange(nullptr);
  if (current != nullptr) {
    current->complete(ch);
  }
}

template <unifex::receiver_of<char> Rec>
struct keyclick_operation : pending_completion {
  Rec rec_;

  explicit keyclick_operation(Rec rec) : rec_(std::move(rec)){}

  void complete(char ch) override final {
    if (ch == CTRL_C)
      unifex::set_done(std::move(rec_));
    else
      unifex::set_value(std::move(rec_), ch);
  }

  void start() noexcept {
    // Enqueue the operation
    auto* previous = pending_completion_.exchange(this);
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

// This is an equivalent implementation of read_keyclick that uses
// a utility from libunifex to construct the sender:
//template <unifex::receiver_of<char> Rec>
//struct keyclick_state : pending_completion {
//  Rec& rec_;
//  explicit keyclick_state(Rec& rec) : rec_(rec) {
//    // Enqueue the operation
//    auto* previous = pending_completion_.exchange(this);
//    assert(previous == nullptr);
//  }
//  void complete(char ch) override final {
//    if (ch == CTRL_C)
//      unifex::set_done(std::move(rec_));
//    else
//      unifex::set_value(std::move(rec_), ch);
//  }
//  keyclick_state(keyclick_state&&) = delete; // immovable
//};
//
//auto read_keyclick() {
//  return unifex::create_simple<char>(
//    [](unifex::receiver_of<char> auto& r) {
//      return keyclick_state{r};
//    });
//}

int main() {
  register_keyboard_callback(on_keyclick);

   auto read_next_char = read_keyclick() |
      unifex::then([](char ch) { printf("In then with char: %c\n", ch); });

  (void)unifex::sync_wait(read_next_char);
}
