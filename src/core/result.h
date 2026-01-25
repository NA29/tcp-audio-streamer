#pragma once

#include "error.h"
#include <cassert>
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
  // special member move/move assignment constructors
  Result(Result &&) = delete;
  Result &operator=(Result &&) = delete;

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
