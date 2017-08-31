#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>

#include "invoke.h"

namespace portable_concurrency {
inline namespace cxx14_v1 {

namespace detail {

template<typename T>
struct is_std_function: std::false_type {};

template<typename S>
struct is_std_function<std::function<S>>: std::true_type {};

template<typename R, typename... A>
class invokable {
public:
  virtual R invoke(A... a) = 0;
};

template<typename F, typename R, typename... A>
class invokable_wrapper final: public invokable<R, A...> {
public:
  invokable_wrapper(F&& f): func_(std::forward<F>(f)) {}
  R invoke(A... a) override {return portable_concurrency::cxx14_v1::detail::invoke(func_, a...);}

private:
  std::decay_t<F> func_;
};

} // namespace detail

template<typename S>
class unique_function;

// Move-only type erasure for arbitrary callable object. Implementation of
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4543.pdf
template<typename R, typename... A>
class unique_function<R(A...)> {
  enum class stored_type {empty, std_function, heap_invokable};
  struct empty_t {};

  template<typename F>
  using stored_type_t = std::integral_constant<
    stored_type,
    std::is_same<F, std::nullptr_t>::value ?
      stored_type::empty:
      (
        detail::is_std_function<F>::value ||
        std::is_function<F>::value ||
        std::is_function<std::remove_pointer_t<F>>::value ||
        detail::is_reference_wrapper<F>::value
      )?
        stored_type::std_function:
        stored_type::heap_invokable
  >;
public:
  unique_function() noexcept {}

  template<typename F>
  unique_function(F&& f): unique_function() {
    init(std::forward<F>(f));
  }

  ~unique_function() {
    destroy_stored_object();
  }

  unique_function(unique_function&& rhs) noexcept(std::is_nothrow_move_constructible<std::function<R(A...)>>::value):
    unique_function()
  {
    switch (rhs.type_) {
    case stored_type::empty: return;

    case stored_type::std_function:
      new (&std_function_) std::function<R(A...)>{std::move(rhs.std_function_)};
      type_ = stored_type::std_function;
    return;

    case stored_type::heap_invokable:
      new (&heap_invokable_) std::unique_ptr<detail::invokable<R, A...>>{std::move(rhs.heap_invokable_)};
      type_ = stored_type::heap_invokable;
    return;
    }
  }

  unique_function& operator= (unique_function&& rhs)
    noexcept(std::is_nothrow_move_assignable<std::function<R(A...)>>::value)
  {
    if (type_ == rhs.type_) {
      switch (type_) {
      case stored_type::empty: break;
      case stored_type::std_function: std_function_ = std::move(rhs.std_function_); break;
      case stored_type::heap_invokable: heap_invokable_ = std::move(rhs.heap_invokable_); break;
      }
      return *this;
    }

    destroy_stored_object();
    type_ = stored_type::empty;

    switch (rhs.type_) {
    case stored_type::empty: break;

    case stored_type::std_function:
      new (&std_function_) std::function<R(A...)>{std::move(rhs.std_function_)};
      type_ = stored_type::std_function;
    break;

    case stored_type::heap_invokable:
      new (&heap_invokable_) std::unique_ptr<detail::invokable<R, A...>>{std::move(rhs.heap_invokable_)};
      type_ = stored_type::heap_invokable;
    break;
    }
    return *this;
  }

  R operator() (A... args) {
    switch (type_) {
    case stored_type::empty: break;
    case stored_type::std_function: return std_function_(args...);
    case stored_type::heap_invokable:
      if (heap_invokable_)
        return heap_invokable_->invoke(args...);
    break;
    }
    throw std::bad_function_call{};
  }

  explicit operator bool () const {
    switch (type_) {
    case stored_type::empty: break;
    case stored_type::std_function: return static_cast<bool>(std_function_);
    case stored_type::heap_invokable: return static_cast<bool>(heap_invokable_);
    }
    return false;
  }

private:
  void destroy_stored_object() noexcept {
    switch (type_) {
    case stored_type::empty: return;
    case stored_type::std_function: std_function_.~function(); return;
    case stored_type::heap_invokable: heap_invokable_.~unique_ptr(); return;
    }
  }

  template<typename F>
  std::enable_if_t<stored_type_t<F>::value == stored_type::empty> init(F&&) {
    assert(type_ == stored_type::empty);
  }

  template<typename F>
  std::enable_if_t<stored_type_t<std::decay_t<F>>::value == stored_type::std_function> init(F&& f) {
    assert(type_ == stored_type::empty);
    new (&std_function_) std::function<R(A...)>(std::forward<F>(f));
    type_ = stored_type::std_function;
  }

  template<typename F>
  std::enable_if_t<stored_type_t<std::decay_t<F>>::value == stored_type::heap_invokable> init(F&& f) {
    assert(type_ == stored_type::empty);
    new (&heap_invokable_) std::unique_ptr<detail::invokable<R, A...>>{
      std::make_unique<detail::invokable_wrapper<F, R, A...>>(std::forward<F>(f))
    };
    type_ = stored_type::heap_invokable;
  }

private:
  stored_type type_ = stored_type::empty;
  union {
    empty_t empty_;
    std::function<R(A...)> std_function_;
    std::unique_ptr<detail::invokable<R, A...>> heap_invokable_;
  };
};

} // inline namespace cxx14_v1
} // namespace portable_concurrency
