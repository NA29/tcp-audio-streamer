#pragma once

#include "error.h"
#include <cassert>
#include <new>          // placement new
#include <type_traits>
#include <utility>

template <typename T> class Result {
private:
  bool ok_;

  // only one can exist at once
  union {
    T value_;
    Error error_;
  };

public:
  // destructor
  ~Result() noexcept {
    if (ok_) {
      value_.~T(); // call the destructor of the value type
    } else {
      error_.~Error();
    }
  }

  // constructors for contained type
  Result(const T &value) : ok_(true), value_(value) {}
  Result(const Error &error) : ok_(false), error_(error) {}

  // move constructors for contained type
  Result(T &&value) : ok_(true), value_(std::move(value)) {}
  Result(Error &&error) : ok_(false), error_(std::move(error)) {}

  // special member copy/copy assignment constructors
  Result(const Result &) = delete;
  Result &operator=(const Result &) = delete;
  // Move construction: a union has no idea which member is alive, so WE must
  // construct the correct one by hand, into raw storage, with placement new.
  Result(Result &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
      : ok_(other.ok_) {
    if (ok_) {
      new (&value_) T(std::move(other.value_));
    } else {
      new (&error_) Error(std::move(other.error_));
    }
  }

  // Move assignment: destroy whichever member we currently hold, THEN construct the
  // incoming one. Getting this order wrong leaks (skip the destructor) or corrupts
  // (construct over a live object).
  Result &operator=(Result &&other) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (this != &other) {
      if (ok_) {
        value_.~T();
      } else {
        error_.~Error();
      }
      ok_ = other.ok_;
      if (ok_) {
        new (&value_) T(std::move(other.value_));
      } else {
        new (&error_) Error(std::move(other.error_));
      }
    }
    return *this;
  }

  // accessors
  T &value() { // non-const so we can modify value of non-const Result
    assert(ok_);
    return value_;
  }

  const T &value() const { // const so we can SEE value of const Result
    assert(ok_);
    return value_;
  }

  const Error &error() const {
    assert(!ok_);
    return error_;
  }

  bool is_ok() const { return ok_; } // const indicates that `this` is const

  explicit operator bool() const { return ok_; } // allows '!'
};

// Result<T> needs a T. For operations that either succeed with no value or fail with an
// Error, we use this empty tag type: Status == Result<Unit>.
//   return Unit{};                      // success
//   return Error{Code::X, "..."};       // failure
struct Unit {};
using Status = Result<Unit>;
