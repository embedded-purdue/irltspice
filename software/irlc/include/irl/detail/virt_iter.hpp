#pragma once

#include <iterator>

namespace irl::detail {

template <class T>
concept Iterable = requires(T a) {
    { a.begin() } -> std::input_iterator;
    { a.end() } -> std::input_iterator;
};

template <Iterable T> using IterT = decltype(std::declval<T>().begin());

template <class T, class OutT>
concept YieldsRef = requires(T c) {
    { *c } -> std::convertible_to<OutT const &>;
};

template <class T, class OutT>
concept YieldMakes = requires(T c) { OutT{*c}; };

// A virtualized input iter for type-erasure
template <typename OutT> struct VirtInputIter {
    virtual void operator++() = 0;
    virtual bool operator==(VirtInputIter const &other) = 0;
    virtual const OutT &operator*() = 0;
};

// A class that inherits from the virtualized input iter
// Takes a normal input_iterator which it stores and auto-implements the necessary operators
template <std::input_iterator BackingIter, typename OutT>
struct ConcreteInputIter final : public VirtInputIter<OutT> {
    using VIter = VirtInputIter<OutT>;
    BackingIter mThis;

    ConcreteInputIter(BackingIter in) : mThis(in) {};

    virtual void operator++() override { mThis++; };
    virtual bool operator==(VIter const &other) override {
        return mThis == static_cast<ConcreteInputIter const &>(other).mThis;
    };

    inline const OutT &deref()
        requires YieldsRef<BackingIter, OutT>
    {
        return *mThis;
    }
    const OutT &deref()
        requires(!YieldsRef<BackingIter, OutT>) && YieldMakes<BackingIter, OutT>
    {
        static OutT tmp;
        tmp.~ProgrammingCommand();
        new (&tmp) OutT(*mThis);
        return tmp;
    }

    virtual const OutT &operator*() override { return deref(); };
};

} // namespace irl::detail
