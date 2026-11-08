/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * @tc.name:iterator
 * @tc.desc:test closeiterator
 * @tc.type: FUNC
 * @tc.require: issue#13777
 */

// IteratorClose test suite
// Each case is split into its own {} block, covering the various call branches
// of JSIterator::IteratorClose.
// Probe iterators use accessor getters for next / return / throw, so the test can
// assert whether each getter (and each method) was actually accessed / called.

// ── Common helpers ─────────────────────────────────────────────
const MAX_ITERATIONS = 5; // by default, done after at most 5 iterations

function makeStats() {
  return {
    symbolIterator: 0, // [Symbol.iterator]() call count
    getNext: 0,        // get next access count
    callNext: 0,       // next() call count
    getReturn: 0,      // get return access count
    callReturn: 0,     // return() call count
    getThrow: 0,       // get throw access count
    callThrow: 0,      // throw() call count
  };
}

// Create a fully-probed iterator object:
// every get next / get return / get throw access and every method call is counted.
// The method factories are flattened to top level to keep the function nesting
// depth within the codecheck limit; probe behavior is unchanged.
function makeProbeNextMethod(opts, stats, counter) {
  if (opts.nextImpl) {
    return function (...args) {
      stats.callNext++;
      return opts.nextImpl.apply(this, args);
    };
  }
  return function () {
    stats.callNext++;
    if (opts.throwOnSecondNext && counter.i === 1) {
      counter.i++;
      throw new Error("next() throws");
    }
    if (counter.i >= MAX_ITERATIONS) {
      return { value: undefined, done: true };
    }
    return { value: counter.i++, done: false };
  };
}

function makeProbeReturnMethod(opts, stats) {
  return function () {
    stats.callReturn++;
    if (opts.returnThrows) {
      throw new Error("return() throws");
    }
    return { done: true };
  };
}

function makeProbeThrowMethod(stats) {
  return function (e) {
    stats.callThrow++;
    throw e;
  };
}

function makeProbeIterator(opts, stats) {
  opts = opts || {};
  // counter lives outside the getter, persists across multiple get next accesses
  const counter = { i: 0 };
  return {
    get next() {
      stats.getNext++;
      return makeProbeNextMethod(opts, stats, counter);
    },
    get return() {
      stats.getReturn++;
      if ("returnValue" in opts) {
        return opts.returnValue; // may be undefined/null/non-function, to cover internal branches
      }
      return makeProbeReturnMethod(opts, stats);
    },
    get throw() {
      stats.getThrow++;
      return makeProbeThrowMethod(stats);
    }
  };
}

// Generic probe iterable (at most 5 iterations by default, all property accesses counted)
function makeIterable(opts, stats) {
  return {
    [Symbol.iterator]() {
      stats.symbolIterator++;
      return makeProbeIterator(opts, stats);
    }
  };
}

// Iterable whose next() throws on the second call (used by Promise combinators)
function throwingIterable(stats) {
  return makeIterable({ throwOnSecondNext: true }, stats);
}

// Run fn(stats) and capture the synchronous outcome plus the probe counters.
function capture(fn) {
  const stats = makeStats();
  const r = { thrown: false, type: "", message: "", stats: stats };
  try {
    fn(stats);
  } catch (e) {
    r.thrown = true;
    r.type = (e && e.constructor) ? e.constructor.name : typeof e;
    r.message = (e && e.message != null) ? String(e.message) : String(e);
  }
  return r;
}

// ═══════════════════════════════════════════════════════════
// Branch 1: bytecode CLOSEITERATOR (for-of early exit)
// Path: interpreter closeiterator bytecode -> SlowRuntimeStub::CloseIterator
//       -> RuntimeStubs::RuntimeCloseIterator -> JSIterator::IteratorClose
// Note: on this path the JS `get return` accessor IS invoked (getReturn = 1).
// ═══════════════════════════════════════════════════════════

// case 1a: for-of + break (completion type = NORMAL)
{
  const r = capture((stats) => {
    for (const x of makeIterable({}, stats)) {
      break;
    }
  });
  assert_true(!r.thrown);
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 1);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 1b: for-of + return inside a function
{
  const r = capture((stats) => {
    function f() {
      for (const x of makeIterable({}, stats)) {
        return 1;
      }
    }
    f();
  });
  assert_true(!r.thrown);
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 1);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 1c: for-of + throw (completion type = THROW); return() succeeds so the
// original "boom" exception is the one that propagates.
{
  const r = capture((stats) => {
    for (const x of makeIterable({}, stats)) {
      throw new Error("boom");
    }
  });
  assert_true(r.thrown);
  assert_equal(r.type, "Error");
  assert_equal(r.message, "boom");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 1);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 1d: array destructuring ends iteration early (takes only the first two elements)
{
  let a, b;
  const r = capture((stats) => {
    [a, b] = makeIterable({}, stats);
  });
  assert_true(!r.thrown);
  assert_equal(a, 0);
  assert_equal(b, 1);
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 2);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 1);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// ═══════════════════════════════════════════════════════════
// Branch 2: Array.from (builtins_array.cpp)
// mapfn throws / CreateDataPropertyOrThrow fails -> IteratorClose
// Note: IteratorClose uses the C++ fast path which does NOT invoke the JS
// and masks the original error.
// ═══════════════════════════════════════════════════════════

// case 2a: Array.from, mapfn throws
{
  const r = capture((stats) => {
    Array.from(makeIterable({}, stats), () => { throw new Error("mapfn"); });
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0); // get return NOT accessed (C++ fast path)
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 2b: Array.from, target array not extensible -> CreateDataPropertyOrThrow fails -> IteratorClose
// Note: Array.from's 3rd argument is the mapfn thisArg; the target object is
//       determined by Array.from's `this` (the constructor).
{
  const r = capture((stats) => {
    class FrozenArray extends Array {
      constructor() {
        super();
        Object.freeze(this);
      }
    }
    Array.from.call(FrozenArray, makeIterable({}, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// ═══════════════════════════════════════════════════════════
// Branch 3: Map constructor (builtins_map.cpp)
// item not object / getting entry[0], entry[1] throws / adder throws
// Same C++ fast-path behavior as branch 2: getReturn stays 0 and the observable
// error is "return function is not Callable".
// ═══════════════════════════════════════════════════════════

// case 3a: iteration item is not an object
{
  const r = capture((stats) => {
    new Map(makeIterable({
      nextImpl: function () {
        return { value: 1, done: false };
      }
    }, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 3b: getting entry[0] (key) throws in its getter
{
  const r = capture((stats) => {
    const badEntry = {
      get 0() { throw new Error("key getter"); },
      1: "v"
    };
    new Map(makeIterable({
      nextImpl: function () {
        return { value: badEntry, done: false };
      }
    }, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 3c: getting entry[1] (value) throws in its getter
{
  const r = capture((stats) => {
    const badEntry = {
      0: "k",
      get 1() { throw new Error("value getter"); }
    };
    new Map(makeIterable({
      nextImpl: function () {
        return { value: badEntry, done: false };
      }
    }, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 3d: adder (Map.set) throws -- subclass overrides set, items are valid [k,v] pairs
{
  const r = capture((stats) => {
    class MyMap extends Map {
      set() { throw new Error("set fail"); }
    }
    let n = 0;
    new MyMap(makeIterable({
      nextImpl: function () {
        if (n >= MAX_ITERATIONS) {
          return { value: undefined, done: true };
        }
        return { value: [`key${n++}`, n], done: false };
      }
    }, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// ═══════════════════════════════════════════════════════════
// Branch 4: Set constructor (builtins_set.cpp)
// adder (Set.add) throws -> IteratorCloseAndReturn
// ═══════════════════════════════════════════════════════════

// case 4: Set subclass overrides add to throw
{
  const r = capture((stats) => {
    class MySet extends Set {
      add() { throw new Error("add fail"); }
    }
    new MySet(makeIterable({}, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// ═══════════════════════════════════════════════════════════
// Branch 5: WeakSet constructor (builtins_weak_set.cpp)
// element not an object -> add throws TypeError -> IteratorCloseAndReturn
// ═══════════════════════════════════════════════════════════

// case 5: WeakSet given a non-object element
{
  const r = capture((stats) => {
    new WeakSet(makeIterable({
      nextImpl: function () {
        return { value: 1, done: false };
      }
    }, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// ═══════════════════════════════════════════════════════════
// Branch 6: Promise combinators (builtins_promise.cpp)
// PerformPromiseXxx returns abrupt while iteratorRecord.done == false -> IteratorClose
// Note: when next() itself throws, the spec sets done = true (Ark builtins_promise.cpp:598
//       and mainstream-engine promise-all inner catch both do this), so IteratorClose is NOT called.
//       Cases 6a-6d therefore override static resolve to throw synchronously, so the
//       exception happens after an element is taken while done is still false, hitting
//       the IteratorClose branch. Each case consumes only 1 element; the throw propagates
//       synchronously out of the combinator.
// ═══════════════════════════════════════════════════════════

// case 6a: Promise.all, constructor.resolve throws synchronously
{
  const r = capture((stats) => {
    class PAll extends Promise {
      static resolve() { throw new Error("resolve throws"); }
    }
    PAll.all(makeIterable({}, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 6b: Promise.race, constructor.resolve throws synchronously
{
  const r = capture((stats) => {
    class PRace extends Promise {
      static resolve() { throw new Error("resolve throws"); }
    }
    PRace.race(makeIterable({}, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 6c: Promise.allSettled, constructor.resolve throws synchronously
{
  const r = capture((stats) => {
    class PSettled extends Promise {
      static resolve() { throw new Error("resolve throws"); }
    }
    PSettled.allSettled(makeIterable({}, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 6d: Promise.any, constructor.resolve throws synchronously
{
  const r = capture((stats) => {
    class PAny extends Promise {
      static resolve() { throw new Error("resolve throws"); }
    }
    PAny.any(makeIterable({}, stats));
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "return function is not Callable");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 0);
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 6e: Promise.all, next() itself throws -> done = true, IteratorClose NOT called.
// Reverse-verification case: getReturn / callReturn must stay 0. The rejection is
// asynchronous, so it does not surface as a synchronous throw (swallowed by .catch).
{
  const r = capture((stats) => {
    Promise.all(throwingIterable(stats)).catch(() => {});
  });
  assert_true(!r.thrown);
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 2);
  assert_equal(r.stats.callNext, 2);
  assert_equal(r.stats.getReturn, 0); // IteratorClose NOT called
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// ═══════════════════════════════════════════════════════════
// Branch 7: coverage of the internal branches of IteratorClose (js_iterator.cpp:234)
// These go through the interpreter path, so the JS `get return` accessor IS invoked.
// ═══════════════════════════════════════════════════════════

// case 7a: iterator has no return method -> completion returned directly (:253)
// get return yields undefined, so return() is never called.
{
  const r = capture((stats) => {
    const iterNoReturn = {
      get next() {
        stats.getNext++;
        return function () {
          stats.callNext++;
          return { value: 1, done: false };
        };
      },
      get return() {
        stats.getReturn++;
        return undefined;
      },
      get throw() {
        stats.getThrow++;
        return undefined;
      }
    };
    for (const x of { [Symbol.iterator]: () => { stats.symbolIterator++; return iterNoReturn; } }) {
      break;
    }
  });
  assert_true(!r.thrown);
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 0); // return is undefined -> not called
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 7b: return property is not callable -> TypeError (:260)
{
  const r = capture((stats) => {
    for (const x of makeIterable({ returnValue: 42 }, stats)) {
      break;
    }
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 0); // not callable -> never called
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 7c: return() returns a non-object -> TypeError "Inner return result is not object" (:288)
{
  const r = capture((stats) => {
    for (const x of makeIterable({
      returnValue: function () {
        stats.callReturn++;
        return 5;
      }
    }, stats)) {
      break;
    }
  });
  assert_true(r.thrown);
  assert_equal(r.type, "TypeError");
  assert_equal(r.message, "Inner return result is not object");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 1);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 7d: return() itself throws + loop body also throws (exceptionOnThread restore logic :241/:268/:278)
// The exception finally caught is the loop body's "body throws".
{
  const r = capture((stats) => {
    for (const x of makeIterable({ returnThrows: true }, stats)) {
      throw new Error("body throws");
    }
  });
  assert_true(r.thrown);
  assert_equal(r.type, "Error");
  assert_equal(r.message, "body throws");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 1); // return() called and threw
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 7e: getting the "return" property throws in its getter (:247 FastGetPropertyByName fails)
{
  const r = capture((stats) => {
    const evil = {
      [Symbol.iterator]: () => {
        stats.symbolIterator++;
        return {
          get next() {
            stats.getNext++;
            return function () {
              stats.callNext++;
              return { value: 1, done: false };
            };
          },
          get return() {
            stats.getReturn++;
            throw new Error("getter throws");
          },
          get throw() {
            stats.getThrow++;
            return undefined;
          }
        };
      }
    };
    for (const x of evil) {
      break;
    }
  });
  assert_true(r.thrown);
  assert_equal(r.type, "Error");
  assert_equal(r.message, "getter throws");
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1); // getter accessed, then threw
  assert_equal(r.stats.callReturn, 0);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 7f: return is null -> same as undefined, completion returned directly (:253)
{
  const r = capture((stats) => {
    for (const x of makeIterable({ returnValue: null }, stats)) {
      break;
    }
  });
  assert_true(!r.thrown);
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 0); // null -> not called
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

// case 7g: normal break + return() returns an object normally (:266 success path, completion NORMAL)
{
  const r = capture((stats) => {
    for (const x of makeIterable({}, stats)) {
      break;
    }
  });
  assert_true(!r.thrown);
  assert_equal(r.stats.symbolIterator, 1);
  assert_equal(r.stats.getNext, 1);
  assert_equal(r.stats.callNext, 1);
  assert_equal(r.stats.getReturn, 1);
  assert_equal(r.stats.callReturn, 1);
  assert_equal(r.stats.getThrow, 0);
  assert_equal(r.stats.callThrow, 0);
}

test_end();
