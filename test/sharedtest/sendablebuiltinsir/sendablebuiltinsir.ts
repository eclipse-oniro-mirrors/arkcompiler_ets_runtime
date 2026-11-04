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
 * @tc.name:sendablebuiltinsir
 * @tc.desc:test sendable builtins ir equivalence (all registered interfaces)
 * @tc.type: FUNC
 */

// @ts-nocheck
declare function print(str: any): string;

class Point {
    x: number = 0;
    y: number = 0;
    constructor(x: number, y: number) {
        "use sendable";
        this.x = x;
        this.y = y;
    }
    PrintStr() {
        print("Point.PrintStr");
    }
    GetSum(): number {
        "use sendable";
        return this.x + this.y;
    }
}

class BigPoint {
    f0: number = 0;
    f1: number = 0;
    f2: number = 0;
    f3: number = 0;
    f4: number = 0;
    f5: number = 0;
    f6: number = 0;
    f7: number = 0;
    constructor() {
        "use sendable";
    }
}

function sfAdd(a: number, b: number): number {
    "use sendable";
    return a + b;
}

function sfId(): number {
    "use sendable";
    return 42;
}

function sfTriple(a: number): number {
    "use sendable";
    return sfAdd.call(null, a, a) + a;
}

// NOTE: shared functions must NOT capture module-level plain let variables (LexicalEnv,
// crashes with IsLexicalEnv assert in lexical_env.h). Only shared functions/classes and
// parameters are reachable inside; statics must be passed in as sendable function args.
function sfScene(o: Point, keysFn: any, gopdFn: any): number {
    "use sendable";
    if (!o.hasOwnProperty("x")) {
        return -1;
    }
    let ks = keysFn(o);
    let sum = 0;
    for (let i = 0; i < ks.length; i++) {
        sum += gopdFn(o, ks[i]).value;
    }
    let viaCall = o.GetSum.call(o);
    let viaBound = o.GetSum.bind(o)();
    return sfAdd.call(null, sum, viaCall) + viaBound;
}

async function sfAsync(a: number): Promise<number> {
    "use sendable";
    return a * 2;
}

let p = new Point(3, 4);
// SharedObject.prototype (p -> Point.prototype -> SharedObject.prototype)
let sop = Object.getPrototypeOf(Object.getPrototypeOf(p));
// SharedFunction.prototype (sfAdd -> SharedFunction.prototype)
let sfp = Object.getPrototypeOf(sfAdd);
// SharedObject constructor (statics holder, not on globalThis)
let SO = sop.constructor;
// SharedFunction constructor
let SF = sfp.constructor;
let notCallable = {};
let bigPoint = new BigPoint();

print("=== G1 hasOwnProperty ===");
print(p.hasOwnProperty("x"));
print(p.hasOwnProperty("notExist"));
print(p.hasOwnProperty("PrintStr"));
print(p.hasOwnProperty(Symbol("s")));
try {
    sop.hasOwnProperty.call(undefined, "x");
} catch (e) {
    print(e.name);
    print(e.message);
}

print("=== G2 toString ===");
print(p.toString());
print(sop.toString.call(sfAdd));
print(sop.toString.call(undefined));
print(sop.toString.call(null));
// type matrix: same stub handles all receivers (boundary)
print(sop.toString.call({}));
print(sop.toString.call([1, 2]));
print(sop.toString.call("s"));
print(sop.toString.call(1));
print(sop.toString.call(true));
print(sop.toString.call(new Error("e")));
print(sop.toString.call(new Date(0)));
print(sop.toString.call(/re/));
(function () { print(sop.toString.call(arguments)); })();
print(sop.toString.call(new Map()));
print(sop.toString.call(new Proxy({}, {})));
print(sop.toString.call(new Proxy([], {})));
print(Object.create({ [Symbol.toStringTag]: "Custom" }).toString());

print("=== G3 statics normal ===");
print(Object.keys(p));
print(Object.entries(p));
print(Object.getOwnPropertyNames(p));
print(Object.getOwnPropertySymbols(p));
let descs = Object.getOwnPropertyDescriptors(p);
print(descs.x.value);
print(descs.x.writable);
print(descs.x.enumerable);
print(descs.x.configurable);
print(Object.getPrototypeOf(p) === Point.prototype);
print(Object.isFrozen(p));
print(Object.isSealed(p));
print(SO.keys(bigPoint));
print(SO.values(bigPoint));
print(Object.isFrozen({}));
print(Object.isSealed({}));
let t = Object.assign({}, p);
print(t.x + "," + t.y);
print(Object.assign({ a: 0 }, p, { y: 9 }).x + "," + Object.assign({ a: 0 }, p, { y: 9 }).y);
try {
    Object.assign(p, { z: 5 });
} catch (e) {
    print(e.name);
    print(e.message);
}
print(p.hasOwnProperty("z"));

print("=== G3 statics exceptions ===");
try { Object.keys(undefined); } catch (e) { print(e.name); print(e.message); }
try { Object.keys(null); } catch (e) { print(e.name); print(e.message); }
try { Object.entries(undefined); } catch (e) { print(e.message); }
try { Object.getOwnPropertyNames(undefined); } catch (e) { print(e.message); }
try { Object.getOwnPropertySymbols(undefined); } catch (e) { print(e.message); }
try { Object.getOwnPropertyDescriptors(undefined); } catch (e) { print(e.message); }
try { Object.getPrototypeOf(undefined); } catch (e) { print(e.message); }
try { Object.getPrototypeOf(null); } catch (e) { print(e.message); }
// isFrozen/isSealed: ES2015 semantics — non-object input returns true WITHOUT throwing
print(Object.isFrozen(undefined));
print(Object.isFrozen(null));
print(Object.isFrozen(1));
print(Object.isSealed(undefined));
print(Object.isSealed(null));
try { Object.setPrototypeOf(p, 1); } catch (e) { print(e.name); print(e.message); }
try { Object.create(1); } catch (e) { print(e.name); print(e.message); }
try { Object.create(p); } catch (e) { print(e.name); print(e.message); }
try { Object.setPrototypeOf(undefined, {}); } catch (e) { print(e.name); print(e.message); }
print(Object.setPrototypeOf(1, {}));

print("=== G4 call/apply ===");
print(sfAdd.call(null, 1, 2));
print(sfAdd.apply(null, [1, 2]));
print(sfAdd.call(null, 1));
try { sfp.call.call(notCallable, null); } catch (e) { print(e.name); print(e.message); }
try { sfp.apply.call(notCallable, null, []); } catch (e) { print(e.name); print(e.message); }
// class constructor callee: message text differs between interpreter modes for ALL
// (sendable and non-sendable) class ctors via call — pre-existing asm/cpp divergence,
// only assert exception type here (behavior aligns with non-sendable Function.prototype.call).
try { Point.call(undefined, 1, 2); } catch (e) { print(e.name); }

// Function vs SharedFunction.prototype.call behavior divergence on class-ctor callees:
// the sendable builtin (via its dedicated stub) routes class-ctor to the C++ slowPath,
// producing "class constructor cannot call" (pre-change message), while the plain
// Function.prototype.call (via the shared IR stub) goes through CallStubBuilder and
// produces "class constructor cannot called without 'new'" — both same TypeError type.
print("=== G4b call class-ctor divergence: SharedFunction vs Function ===");
let fpc = Object.getPrototypeOf(function () {});
try { fpc.call.call(Point, undefined, 1, 2); } catch (e) { print("FP.call(Point): " + e.name); }
try { sfp.call.call(Point, undefined, 1, 2); } catch (e) { print("SFP.call(Point): " + e.name); }
class PlainClass { constructor() {} }
try { fpc.call.call(PlainClass, undefined); } catch (e) { print("FP.call(Plain): " + e.name); }
try { sfp.call.call(PlainClass, undefined); } catch (e) { print("SFP.call(Plain): " + e.name); }
print(fpc.call.call(sfAdd, null, 1, 2));
print(sfp.call.call(sfAdd, null, 1, 2));

print(p.GetSum.call(p));
print(p.GetSum.apply(p, []));
print(sfId.call());
print(sfAdd.call(null, 1, 2, 3));
print(sfAdd.apply(null, { length: 2, 0: 1, 1: 2 }));
print(sfAdd.apply(null, new Int8Array([1, 2])));
(function () { print(sfAdd.apply(null, arguments)); })(1, 2);

print("=== G5 bind ===");
let b = sfAdd.bind(null, 10);
print(b(5));
print(b.length);
print(b.name);
print(sfAdd.bind(null, 1, 2)());
print(sfAdd.bind(null, 1)(2));
print(p.GetSum.bind(p)());
print(b.bind(null, 5).length);
print(sfAdd.bind(null).toString().length > 0);
try { sfp.bind.call(notCallable); } catch (e) { print(e.name); print(e.message); }

print("=== G6 shared callee chain ===");
print(sfTriple(5));

print("=== G7 SharedObject.prototype others ===");
print(p.valueOf() === p);
print(p.propertyIsEnumerable("x"));
print(p.propertyIsEnumerable("PrintStr"));
print(p.isPrototypeOf(new Point(1, 2)));
print(Point.prototype.isPrototypeOf(p));
print(p.toLocaleString());
print(p.__proto__ === Point.prototype);
print(p.propertyIsEnumerable("missing"));
print(Point.prototype.isPrototypeOf(1));
print(Point.prototype.isPrototypeOf(undefined));
try { sop.valueOf.call(undefined); } catch (e) { print(e.name); print(e.message); }
try { sop.propertyIsEnumerable.call(undefined, "x"); } catch (e) { print(e.name); print(e.message); }
try { sop.isPrototypeOf.call(undefined, p); } catch (e) { print(e.name); print(e.message); }
try { sop.toLocaleString.call(undefined); } catch (e) { print(e.name); print(e.message); }
try {
    p.__proto__ = {};
} catch (e) {
    print(e.name);
}
try {
    p.__proto__ = null;
} catch (e) {
    print(e.name);
}

print("=== G8 SharedObject statics via SO (out of scope) ===");
print(SO.keys(p));
print(SO.keys(p).length === Object.keys(p).length);
print(SO.is(p, p));
print(SO.isExtensible(p));
let d1 = SO.getOwnPropertyDescriptor(p, "x");
print(d1.value + "," + d1.writable + "," + d1.enumerable + "," + d1.configurable);
print(SO.getOwnPropertyDescriptor(p, "zz") === undefined);
try { SO.defineProperty(p, "z", { value: 1 }); } catch (e) { print(e.name); print(e.message); }
try { SO.defineProperties(p, { z: { value: 1 } }); } catch (e) { print(e.name); print(e.message); }
print(SO.freeze(p) === p);
print(SO.seal(p) === p);
print(SO.preventExtensions(p) === p);
print(SO.hasOwn(p, "x"));
print(SO.values(p));
print(SO.fromEntries([["a", 1]]).a);
print(SO.is(NaN, NaN));
print(SO.is(0, -0));
print(SO.is("1", 1));
let plainDef = {};
SO.defineProperty(plainDef, "k", { value: 9 });
print(plainDef.k);
let plainFrozen = { a: 1 };
SO.freeze(plainFrozen);
print(SO.isFrozen(plainFrozen));
let nullProto = SO.create(null);
print(nullProto.__proto__ === null);
let plainSTP = {};
print(SO.setPrototypeOf(plainSTP, null) === plainSTP);
print(SO.setPrototypeOf(plainSTP, {}) === plainSTP);

print("=== G9 SharedFunction.prototype others ===");
print(sfAdd.toString().length > 0);
print(sfp.toString.call(sfAdd).length > 0);
print(sfAdd instanceof Point);
print(sfp[Symbol.hasInstance].call(sfAdd, p));
try { sfAdd.caller; } catch (e) { print(e.name); print(e.message); }
try { sfAdd.arguments; } catch (e) { print(e.name); print(e.message); }

print("=== G10 constructors disabled ===");
try { new SO(); } catch (e) { print(e.name); print(e.message); }
try { SO(); } catch (e) { print(e.name); print(e.message); }
try { new SF(); } catch (e) { print(e.name); print(e.message); }
try { SF(); } catch (e) { print(e.name); print(e.message); }

print("=== G12 combined scenarios ===");
// S1 data validation flow: keys -> hasOwnProperty -> getOwnPropertyDescriptor
(function () {
    let ks = SO.keys(p);
    let ok = true;
    let total = 0;
    for (let i = 0; i < ks.length; i++) {
        ok = ok && p.hasOwnProperty(ks[i]);
        total += SO.getOwnPropertyDescriptor(p, ks[i]).value;
    }
    print("S1 " + ok + "," + total);
})();
// S2 snapshot clone: assign vs entries rebuild, then keys/values compare
(function () {
    let snap1 = SO.assign({}, p);
    let snap2 = {};
    let es = SO.entries(p);
    for (let i = 0; i < es.length; i++) {
        snap2[es[i][0]] = es[i][1];
    }
    print("S2 " + (SO.keys(snap1).join("|") === SO.keys(snap2).join("|")) + "," + SO.values(snap2).join(","));
})();
// S3 prototype chain walk: getPrototypeOf upward + toString tag per level
(function () {
    let tags = [];
    let cur = p;
    while (cur !== null && cur !== undefined) {
        tags.push(sop.toString.call(cur));
        cur = SO.getPrototypeOf(cur);
    }
    print("S3 " + tags.join(">") + "," + Point.prototype.isPrototypeOf(p));
})();
// S4 descriptor-driven rebuild: gOPDs keys -> gOPD -> defineProperty -> hasOwn/values
(function () {
    let rebuilt = {};
    let ks = SO.keys(p);
    for (let i = 0; i < ks.length; i++) {
        let d = SO.getOwnPropertyDescriptor(p, ks[i]);
        SO.defineProperty(rebuilt, ks[i], { value: d.value, enumerable: true, writable: true, configurable: true });
    }
    print("S4 " + SO.values(rebuilt).join(",") + "," + SO.hasOwn(rebuilt, "y"));
})();
// S5 functional composition: bind pre-args -> call/apply on bound -> chained bind
(function () {
    let add10 = sfAdd.bind(null, 10);
    print("S5 " + add10.call(null, 5) + "," + add10.apply(null, [6]) + "," + add10.bind(null, 7)());
})();
// S6 callback dispatch: bound shared fn as callback, caller uses call+apply
(function () {
    function twice(fn, arg) {
        return fn.call(null, fn.apply(null, [arg]));
    }
    let addOne = sfAdd.bind(null, 1);
    print("S6 " + twice(addOne, 2));
})();
// S7 serialization log: toString + keys + values assembled
(function () {
    function dump(obj) {
        return sop.toString.call(obj) + ":" + SO.keys(obj).join(",") + "=" + SO.values(obj).join(",");
    }
    print("S7 " + dump(p) + ";" + dump(bigPoint));
})();
// S8 defensive mutation: integrity checks -> failed defineProperty/assign/setPrototypeOf -> unchanged
(function () {
    let before = SO.keys(p).join(",") + "=" + SO.values(p).join(",");
    let report = SO.isFrozen(p) + "," + SO.isSealed(p) + "," + SO.isExtensible(p);
    try { SO.defineProperty(p, "z", { value: 9 }); } catch (e) { report += ",def-throw"; }
    try { SO.assign(p, { z: 9 }); } catch (e) { report += ",assign-throw"; }
    try { SO.setPrototypeOf(p, {}); } catch (e) { report += ",stp-throw"; }
    let after = SO.keys(p).join(",") + "=" + SO.values(p).join(",");
    print("S8 " + report + "," + (before === after));
})();
// S9 composition inside shared function (taskpool-like): hasOwnProperty + keys + gOPD via
// passed-in sendable statics + method this/call/bind + nested call
(function () {
    print("S9 " + sfScene(p, SO.keys, SO.getOwnPropertyDescriptor));
})();
// S10 exception fallback chain: create(sendable) throws -> fallback object -> assign succeeds
(function () {
    let dst;
    try { dst = SO.create(p); } catch (e) { dst = {}; }
    try { SO.setPrototypeOf(dst, 1); } catch (e) { /* ignore */ }
    SO.assign(dst, p);
    print("S10 " + SO.keys(dst).join(",") + ":" + SO.values(dst).join(","));
})();
// S11 iterator composition: entries -> destructured loop -> bound fn accumulation
(function () {
    let pairs = SO.entries(p);
    let acc = sfAdd.bind(null);
    let sum = 0;
    for (let i = 0; i < pairs.length; i++) {
        sum = acc(sum, pairs[i][1]);
    }
    print("S11 " + sum);
})();
// S12 cross-validation: keys/entries/values/gOPN length consistency on sendable
(function () {
    print("S12 " + (SO.keys(p).length === SO.entries(p).length) + "," +
        (SO.keys(p).length === SO.values(p).length) + "," +
        (SO.getOwnPropertyNames(p).length === SO.keys(p).length));
})();

// sendable sources with dictionary elements
// (numeric-named class fields) or dictionary properties (more than MAX_INLINE fields) must
// keep the pre-change C++ assignment order — element keys ascending, named keys in
// declaration order — observable through target side effects and exception positions.

class SrcElem {
    "77": number = 77;
    "3": number = 3;
    "150": number = 150;
    "42": number = 42;
    "8": number = 8;
    "300": number = 300;
    "21": number = 21;
    "99": number = 99;
    constructor() { "use sendable"; }
}

class SrcMix {
    "77": number = 77;
    name: string = "n";
    "3": number = 3;
    tag: string = "t";
    constructor() { "use sendable"; }
}

class DictSrc {
    f0: number = 0;
    f1: number = 0;
    f2: number = 0;
    f3: number = 0;
    f4: number = 0;
    f5: number = 0;
    f6: number = 0;
    f7: number = 0;
    f8: number = 0;
    f9: number = 0;
    f10: number = 0;
    f11: number = 0;
    f12: number = 0;
    f13: number = 0;
    f14: number = 0;
    f15: number = 0;
    f16: number = 0;
    f17: number = 0;
    f18: number = 0;
    f19: number = 0;
    f20: number = 0;
    f21: number = 0;
    f22: number = 0;
    f23: number = 0;
    f24: number = 0;
    f25: number = 0;
    f26: number = 0;
    f27: number = 0;
    f28: number = 0;
    f29: number = 0;
    f30: number = 0;
    f31: number = 0;
    f32: number = 0;
    f33: number = 0;
    f34: number = 0;
    f35: number = 0;
    f36: number = 0;
    f37: number = 0;
    f38: number = 0;
    f39: number = 0;
    f40: number = 0;
    f41: number = 0;
    f42: number = 0;
    f43: number = 0;
    f44: number = 0;
    f45: number = 0;
    f46: number = 0;
    f47: number = 0;
    f48: number = 0;
    f49: number = 0;
    f50: number = 0;
    f51: number = 0;
    f52: number = 0;
    f53: number = 0;
    f54: number = 0;
    f55: number = 0;
    f56: number = 0;
    f57: number = 0;
    f58: number = 0;
    f59: number = 0;
    f60: number = 0;
    f61: number = 0;
    f62: number = 0;
    f63: number = 0;
    f64: number = 0;
    f65: number = 0;
    f66: number = 0;
    f67: number = 0;
    f68: number = 0;
    f69: number = 0;
    f70: number = 0;
    f71: number = 0;
    f72: number = 0;
    f73: number = 0;
    f74: number = 0;
    f75: number = 0;
    f76: number = 0;
    f77: number = 0;
    f78: number = 0;
    f79: number = 0;
    f80: number = 0;
    f81: number = 0;
    f82: number = 0;
    f83: number = 0;
    f84: number = 0;
    f85: number = 0;
    f86: number = 0;
    f87: number = 0;
    f88: number = 0;
    f89: number = 0;
    f90: number = 0;
    f91: number = 0;
    f92: number = 0;
    f93: number = 0;
    f94: number = 0;
    f95: number = 0;
    f96: number = 0;
    f97: number = 0;
    f98: number = 0;
    f99: number = 0;
    f100: number = 0;
    f101: number = 0;
    f102: number = 0;
    f103: number = 0;
    f104: number = 0;
    f105: number = 0;
    f106: number = 0;
    f107: number = 0;
    f108: number = 0;
    f109: number = 0;
    f110: number = 0;
    f111: number = 0;
    f112: number = 0;
    f113: number = 0;
    f114: number = 0;
    f115: number = 0;
    f116: number = 0;
    f117: number = 0;
    f118: number = 0;
    f119: number = 0;
    f120: number = 0;
    f121: number = 0;
    f122: number = 0;
    f123: number = 0;
    f124: number = 0;
    f125: number = 0;
    f126: number = 0;
    f127: number = 0;
    f128: number = 0;
    f129: number = 0;
    f130: number = 0;
    f131: number = 0;
    f132: number = 0;
    f133: number = 0;
    f134: number = 0;
    f135: number = 0;
    f136: number = 0;
    f137: number = 0;
    f138: number = 0;
    f139: number = 0;
    f140: number = 0;
    f141: number = 0;
    f142: number = 0;
    f143: number = 0;
    f144: number = 0;
    f145: number = 0;
    f146: number = 0;
    f147: number = 0;
    f148: number = 0;
    f149: number = 0;
    f150: number = 0;
    f151: number = 0;
    f152: number = 0;
    f153: number = 0;
    f154: number = 0;
    f155: number = 0;
    f156: number = 0;
    f157: number = 0;
    f158: number = 0;
    f159: number = 0;
    f160: number = 0;
    f161: number = 0;
    f162: number = 0;
    f163: number = 0;
    f164: number = 0;
    f165: number = 0;
    f166: number = 0;
    f167: number = 0;
    f168: number = 0;
    f169: number = 0;
    f170: number = 0;
    f171: number = 0;
    f172: number = 0;
    f173: number = 0;
    f174: number = 0;
    f175: number = 0;
    f176: number = 0;
    f177: number = 0;
    f178: number = 0;
    f179: number = 0;
    f180: number = 0;
    f181: number = 0;
    f182: number = 0;
    f183: number = 0;
    f184: number = 0;
    f185: number = 0;
    f186: number = 0;
    f187: number = 0;
    f188: number = 0;
    f189: number = 0;
    f190: number = 0;
    f191: number = 0;
    f192: number = 0;
    f193: number = 0;
    f194: number = 0;
    f195: number = 0;
    f196: number = 0;
    f197: number = 0;
    f198: number = 0;
    f199: number = 0;
    f200: number = 0;
    f201: number = 0;
    f202: number = 0;
    f203: number = 0;
    f204: number = 0;
    f205: number = 0;
    f206: number = 0;
    f207: number = 0;
    f208: number = 0;
    f209: number = 0;
    f210: number = 0;
    f211: number = 0;
    f212: number = 0;
    f213: number = 0;
    f214: number = 0;
    f215: number = 0;
    f216: number = 0;
    f217: number = 0;
    f218: number = 0;
    f219: number = 0;
    f220: number = 0;
    f221: number = 0;
    f222: number = 0;
    f223: number = 0;
    f224: number = 0;
    f225: number = 0;
    f226: number = 0;
    f227: number = 0;
    f228: number = 0;
    f229: number = 0;
    f230: number = 0;
    f231: number = 0;
    f232: number = 0;
    f233: number = 0;
    f234: number = 0;
    f235: number = 0;
    f236: number = 0;
    f237: number = 0;
    f238: number = 0;
    f239: number = 0;
    f240: number = 0;
    f241: number = 0;
    f242: number = 0;
    f243: number = 0;
    f244: number = 0;
    f245: number = 0;
    f246: number = 0;
    f247: number = 0;
    f248: number = 0;
    f249: number = 0;
    f250: number = 0;
    f251: number = 0;
    f252: number = 0;
    f253: number = 0;
    f254: number = 0;
    f255: number = 0;
    f256: number = 0;
    f257: number = 0;
    f258: number = 0;
    f259: number = 0;
    f260: number = 0;
    f261: number = 0;
    f262: number = 0;
    f263: number = 0;
    f264: number = 0;
    f265: number = 0;
    f266: number = 0;
    f267: number = 0;
    f268: number = 0;
    f269: number = 0;
    f270: number = 0;
    f271: number = 0;
    f272: number = 0;
    f273: number = 0;
    f274: number = 0;
    f275: number = 0;
    f276: number = 0;
    f277: number = 0;
    f278: number = 0;
    f279: number = 0;
    f280: number = 0;
    f281: number = 0;
    f282: number = 0;
    f283: number = 0;
    f284: number = 0;
    f285: number = 0;
    f286: number = 0;
    f287: number = 0;
    f288: number = 0;
    f289: number = 0;
    f290: number = 0;
    f291: number = 0;
    f292: number = 0;
    f293: number = 0;
    f294: number = 0;
    f295: number = 0;
    f296: number = 0;
    f297: number = 0;
    f298: number = 0;
    f299: number = 0;
    f300: number = 0;
    f301: number = 0;
    f302: number = 0;
    f303: number = 0;
    f304: number = 0;
    f305: number = 0;
    f306: number = 0;
    f307: number = 0;
    f308: number = 0;
    f309: number = 0;
    f310: number = 0;
    f311: number = 0;
    f312: number = 0;
    f313: number = 0;
    f314: number = 0;
    f315: number = 0;
    f316: number = 0;
    f317: number = 0;
    f318: number = 0;
    f319: number = 0;
    f320: number = 0;
    f321: number = 0;
    f322: number = 0;
    f323: number = 0;
    f324: number = 0;
    f325: number = 0;
    f326: number = 0;
    f327: number = 0;
    f328: number = 0;
    f329: number = 0;
    f330: number = 0;
    f331: number = 0;
    f332: number = 0;
    f333: number = 0;
    f334: number = 0;
    f335: number = 0;
    f336: number = 0;
    f337: number = 0;
    f338: number = 0;
    f339: number = 0;
    f340: number = 0;
    f341: number = 0;
    f342: number = 0;
    f343: number = 0;
    f344: number = 0;
    f345: number = 0;
    f346: number = 0;
    f347: number = 0;
    f348: number = 0;
    f349: number = 0;
    f350: number = 0;
    f351: number = 0;
    f352: number = 0;
    f353: number = 0;
    f354: number = 0;
    f355: number = 0;
    f356: number = 0;
    f357: number = 0;
    f358: number = 0;
    f359: number = 0;
    f360: number = 0;
    f361: number = 0;
    f362: number = 0;
    f363: number = 0;
    f364: number = 0;
    f365: number = 0;
    f366: number = 0;
    f367: number = 0;
    f368: number = 0;
    f369: number = 0;
    f370: number = 0;
    f371: number = 0;
    f372: number = 0;
    f373: number = 0;
    f374: number = 0;
    f375: number = 0;
    f376: number = 0;
    f377: number = 0;
    f378: number = 0;
    f379: number = 0;
    f380: number = 0;
    f381: number = 0;
    f382: number = 0;
    f383: number = 0;
    f384: number = 0;
    f385: number = 0;
    f386: number = 0;
    f387: number = 0;
    f388: number = 0;
    f389: number = 0;
    f390: number = 0;
    f391: number = 0;
    f392: number = 0;
    f393: number = 0;
    f394: number = 0;
    f395: number = 0;
    f396: number = 0;
    f397: number = 0;
    f398: number = 0;
    f399: number = 0;
    f400: number = 0;
    f401: number = 0;
    f402: number = 0;
    f403: number = 0;
    f404: number = 0;
    f405: number = 0;
    f406: number = 0;
    f407: number = 0;
    f408: number = 0;
    f409: number = 0;
    f410: number = 0;
    f411: number = 0;
    f412: number = 0;
    f413: number = 0;
    f414: number = 0;
    f415: number = 0;
    f416: number = 0;
    f417: number = 0;
    f418: number = 0;
    f419: number = 0;
    f420: number = 0;
    f421: number = 0;
    f422: number = 0;
    f423: number = 0;
    f424: number = 0;
    f425: number = 0;
    f426: number = 0;
    f427: number = 0;
    f428: number = 0;
    f429: number = 0;
    f430: number = 0;
    f431: number = 0;
    f432: number = 0;
    f433: number = 0;
    f434: number = 0;
    f435: number = 0;
    f436: number = 0;
    f437: number = 0;
    f438: number = 0;
    f439: number = 0;
    f440: number = 0;
    f441: number = 0;
    f442: number = 0;
    f443: number = 0;
    f444: number = 0;
    f445: number = 0;
    f446: number = 0;
    f447: number = 0;
    f448: number = 0;
    f449: number = 0;
    f450: number = 0;
    f451: number = 0;
    f452: number = 0;
    f453: number = 0;
    f454: number = 0;
    f455: number = 0;
    f456: number = 0;
    f457: number = 0;
    f458: number = 0;
    f459: number = 0;
    f460: number = 0;
    f461: number = 0;
    f462: number = 0;
    f463: number = 0;
    f464: number = 0;
    f465: number = 0;
    f466: number = 0;
    f467: number = 0;
    f468: number = 0;
    f469: number = 0;
    f470: number = 0;
    f471: number = 0;
    f472: number = 0;
    f473: number = 0;
    f474: number = 0;
    f475: number = 0;
    f476: number = 0;
    f477: number = 0;
    f478: number = 0;
    f479: number = 0;
    f480: number = 0;
    f481: number = 0;
    f482: number = 0;
    f483: number = 0;
    f484: number = 0;
    f485: number = 0;
    f486: number = 0;
    f487: number = 0;
    f488: number = 0;
    f489: number = 0;
    f490: number = 0;
    f491: number = 0;
    f492: number = 0;
    f493: number = 0;
    f494: number = 0;
    f495: number = 0;
    f496: number = 0;
    f497: number = 0;
    f498: number = 0;
    f499: number = 0;
    f500: number = 0;
    f501: number = 0;
    f502: number = 0;
    f503: number = 0;
    f504: number = 0;
    f505: number = 0;
    f506: number = 0;
    f507: number = 0;
    f508: number = 0;
    f509: number = 0;
    f510: number = 0;
    f511: number = 0;
    f512: number = 0;
    f513: number = 0;
    f514: number = 0;
    f515: number = 0;
    f516: number = 0;
    f517: number = 0;
    f518: number = 0;
    f519: number = 0;
    f520: number = 0;
    f521: number = 0;
    f522: number = 0;
    f523: number = 0;
    f524: number = 0;
    f525: number = 0;
    f526: number = 0;
    f527: number = 0;
    f528: number = 0;
    f529: number = 0;
    f530: number = 0;
    f531: number = 0;
    f532: number = 0;
    f533: number = 0;
    f534: number = 0;
    f535: number = 0;
    f536: number = 0;
    f537: number = 0;
    f538: number = 0;
    f539: number = 0;
    f540: number = 0;
    f541: number = 0;
    f542: number = 0;
    f543: number = 0;
    f544: number = 0;
    f545: number = 0;
    f546: number = 0;
    f547: number = 0;
    f548: number = 0;
    f549: number = 0;
    f550: number = 0;
    f551: number = 0;
    f552: number = 0;
    f553: number = 0;
    f554: number = 0;
    f555: number = 0;
    f556: number = 0;
    f557: number = 0;
    f558: number = 0;
    f559: number = 0;
    f560: number = 0;
    f561: number = 0;
    f562: number = 0;
    f563: number = 0;
    f564: number = 0;
    f565: number = 0;
    f566: number = 0;
    f567: number = 0;
    f568: number = 0;
    f569: number = 0;
    f570: number = 0;
    f571: number = 0;
    f572: number = 0;
    f573: number = 0;
    f574: number = 0;
    f575: number = 0;
    f576: number = 0;
    f577: number = 0;
    f578: number = 0;
    f579: number = 0;
    f580: number = 0;
    f581: number = 0;
    f582: number = 0;
    f583: number = 0;
    f584: number = 0;
    f585: number = 0;
    f586: number = 0;
    f587: number = 0;
    f588: number = 0;
    f589: number = 0;
    f590: number = 0;
    f591: number = 0;
    f592: number = 0;
    f593: number = 0;
    f594: number = 0;
    f595: number = 0;
    f596: number = 0;
    f597: number = 0;
    f598: number = 0;
    f599: number = 0;
    f600: number = 0;
    f601: number = 0;
    f602: number = 0;
    f603: number = 0;
    f604: number = 0;
    f605: number = 0;
    f606: number = 0;
    f607: number = 0;
    f608: number = 0;
    f609: number = 0;
    f610: number = 0;
    f611: number = 0;
    f612: number = 0;
    f613: number = 0;
    f614: number = 0;
    f615: number = 0;
    f616: number = 0;
    f617: number = 0;
    f618: number = 0;
    f619: number = 0;
    f620: number = 0;
    f621: number = 0;
    f622: number = 0;
    f623: number = 0;
    f624: number = 0;
    f625: number = 0;
    f626: number = 0;
    f627: number = 0;
    f628: number = 0;
    f629: number = 0;
    f630: number = 0;
    f631: number = 0;
    f632: number = 0;
    f633: number = 0;
    f634: number = 0;
    f635: number = 0;
    f636: number = 0;
    f637: number = 0;
    f638: number = 0;
    f639: number = 0;
    f640: number = 0;
    f641: number = 0;
    f642: number = 0;
    f643: number = 0;
    f644: number = 0;
    f645: number = 0;
    f646: number = 0;
    f647: number = 0;
    f648: number = 0;
    f649: number = 0;
    f650: number = 0;
    f651: number = 0;
    f652: number = 0;
    f653: number = 0;
    f654: number = 0;
    f655: number = 0;
    f656: number = 0;
    f657: number = 0;
    f658: number = 0;
    f659: number = 0;
    f660: number = 0;
    f661: number = 0;
    f662: number = 0;
    f663: number = 0;
    f664: number = 0;
    f665: number = 0;
    f666: number = 0;
    f667: number = 0;
    f668: number = 0;
    f669: number = 0;
    f670: number = 0;
    f671: number = 0;
    f672: number = 0;
    f673: number = 0;
    f674: number = 0;
    f675: number = 0;
    f676: number = 0;
    f677: number = 0;
    f678: number = 0;
    f679: number = 0;
    f680: number = 0;
    f681: number = 0;
    f682: number = 0;
    f683: number = 0;
    f684: number = 0;
    f685: number = 0;
    f686: number = 0;
    f687: number = 0;
    f688: number = 0;
    f689: number = 0;
    f690: number = 0;
    f691: number = 0;
    f692: number = 0;
    f693: number = 0;
    f694: number = 0;
    f695: number = 0;
    f696: number = 0;
    f697: number = 0;
    f698: number = 0;
    f699: number = 0;
    f700: number = 0;
    f701: number = 0;
    f702: number = 0;
    f703: number = 0;
    f704: number = 0;
    f705: number = 0;
    f706: number = 0;
    f707: number = 0;
    f708: number = 0;
    f709: number = 0;
    f710: number = 0;
    f711: number = 0;
    f712: number = 0;
    f713: number = 0;
    f714: number = 0;
    f715: number = 0;
    f716: number = 0;
    f717: number = 0;
    f718: number = 0;
    f719: number = 0;
    f720: number = 0;
    f721: number = 0;
    f722: number = 0;
    f723: number = 0;
    f724: number = 0;
    f725: number = 0;
    f726: number = 0;
    f727: number = 0;
    f728: number = 0;
    f729: number = 0;
    f730: number = 0;
    f731: number = 0;
    f732: number = 0;
    f733: number = 0;
    f734: number = 0;
    f735: number = 0;
    f736: number = 0;
    f737: number = 0;
    f738: number = 0;
    f739: number = 0;
    f740: number = 0;
    f741: number = 0;
    f742: number = 0;
    f743: number = 0;
    f744: number = 0;
    f745: number = 0;
    f746: number = 0;
    f747: number = 0;
    f748: number = 0;
    f749: number = 0;
    f750: number = 0;
    f751: number = 0;
    f752: number = 0;
    f753: number = 0;
    f754: number = 0;
    f755: number = 0;
    f756: number = 0;
    f757: number = 0;
    f758: number = 0;
    f759: number = 0;
    f760: number = 0;
    f761: number = 0;
    f762: number = 0;
    f763: number = 0;
    f764: number = 0;
    f765: number = 0;
    f766: number = 0;
    f767: number = 0;
    f768: number = 0;
    f769: number = 0;
    f770: number = 0;
    f771: number = 0;
    f772: number = 0;
    f773: number = 0;
    f774: number = 0;
    f775: number = 0;
    f776: number = 0;
    f777: number = 0;
    f778: number = 0;
    f779: number = 0;
    f780: number = 0;
    f781: number = 0;
    f782: number = 0;
    f783: number = 0;
    f784: number = 0;
    f785: number = 0;
    f786: number = 0;
    f787: number = 0;
    f788: number = 0;
    f789: number = 0;
    f790: number = 0;
    f791: number = 0;
    f792: number = 0;
    f793: number = 0;
    f794: number = 0;
    f795: number = 0;
    f796: number = 0;
    f797: number = 0;
    f798: number = 0;
    f799: number = 0;
    f800: number = 0;
    f801: number = 0;
    f802: number = 0;
    f803: number = 0;
    f804: number = 0;
    f805: number = 0;
    f806: number = 0;
    f807: number = 0;
    f808: number = 0;
    f809: number = 0;
    f810: number = 0;
    f811: number = 0;
    f812: number = 0;
    f813: number = 0;
    f814: number = 0;
    f815: number = 0;
    f816: number = 0;
    f817: number = 0;
    f818: number = 0;
    f819: number = 0;
    f820: number = 0;
    f821: number = 0;
    f822: number = 0;
    f823: number = 0;
    f824: number = 0;
    f825: number = 0;
    f826: number = 0;
    f827: number = 0;
    f828: number = 0;
    f829: number = 0;
    f830: number = 0;
    f831: number = 0;
    f832: number = 0;
    f833: number = 0;
    f834: number = 0;
    f835: number = 0;
    f836: number = 0;
    f837: number = 0;
    f838: number = 0;
    f839: number = 0;
    f840: number = 0;
    f841: number = 0;
    f842: number = 0;
    f843: number = 0;
    f844: number = 0;
    f845: number = 0;
    f846: number = 0;
    f847: number = 0;
    f848: number = 0;
    f849: number = 0;
    f850: number = 0;
    f851: number = 0;
    f852: number = 0;
    f853: number = 0;
    f854: number = 0;
    f855: number = 0;
    f856: number = 0;
    f857: number = 0;
    f858: number = 0;
    f859: number = 0;
    f860: number = 0;
    f861: number = 0;
    f862: number = 0;
    f863: number = 0;
    f864: number = 0;
    f865: number = 0;
    f866: number = 0;
    f867: number = 0;
    f868: number = 0;
    f869: number = 0;
    f870: number = 0;
    f871: number = 0;
    f872: number = 0;
    f873: number = 0;
    f874: number = 0;
    f875: number = 0;
    f876: number = 0;
    f877: number = 0;
    f878: number = 0;
    f879: number = 0;
    f880: number = 0;
    f881: number = 0;
    f882: number = 0;
    f883: number = 0;
    f884: number = 0;
    f885: number = 0;
    f886: number = 0;
    f887: number = 0;
    f888: number = 0;
    f889: number = 0;
    f890: number = 0;
    f891: number = 0;
    f892: number = 0;
    f893: number = 0;
    f894: number = 0;
    f895: number = 0;
    f896: number = 0;
    f897: number = 0;
    f898: number = 0;
    f899: number = 0;
    f900: number = 0;
    f901: number = 0;
    f902: number = 0;
    f903: number = 0;
    f904: number = 0;
    f905: number = 0;
    f906: number = 0;
    f907: number = 0;
    f908: number = 0;
    f909: number = 0;
    f910: number = 0;
    f911: number = 0;
    f912: number = 0;
    f913: number = 0;
    f914: number = 0;
    f915: number = 0;
    f916: number = 0;
    f917: number = 0;
    f918: number = 0;
    f919: number = 0;
    f920: number = 0;
    f921: number = 0;
    f922: number = 0;
    f923: number = 0;
    f924: number = 0;
    f925: number = 0;
    f926: number = 0;
    f927: number = 0;
    f928: number = 0;
    f929: number = 0;
    f930: number = 0;
    f931: number = 0;
    f932: number = 0;
    f933: number = 0;
    f934: number = 0;
    f935: number = 0;
    f936: number = 0;
    f937: number = 0;
    f938: number = 0;
    f939: number = 0;
    f940: number = 0;
    f941: number = 0;
    f942: number = 0;
    f943: number = 0;
    f944: number = 0;
    f945: number = 0;
    f946: number = 0;
    f947: number = 0;
    f948: number = 0;
    f949: number = 0;
    f950: number = 0;
    f951: number = 0;
    f952: number = 0;
    f953: number = 0;
    f954: number = 0;
    f955: number = 0;
    f956: number = 0;
    f957: number = 0;
    f958: number = 0;
    f959: number = 0;
    f960: number = 0;
    f961: number = 0;
    f962: number = 0;
    f963: number = 0;
    f964: number = 0;
    f965: number = 0;
    f966: number = 0;
    f967: number = 0;
    f968: number = 0;
    f969: number = 0;
    f970: number = 0;
    f971: number = 0;
    f972: number = 0;
    f973: number = 0;
    f974: number = 0;
    f975: number = 0;
    f976: number = 0;
    f977: number = 0;
    f978: number = 0;
    f979: number = 0;
    f980: number = 0;
    f981: number = 0;
    f982: number = 0;
    f983: number = 0;
    f984: number = 0;
    f985: number = 0;
    f986: number = 0;
    f987: number = 0;
    f988: number = 0;
    f989: number = 0;
    f990: number = 0;
    f991: number = 0;
    f992: number = 0;
    f993: number = 0;
    f994: number = 0;
    f995: number = 0;
    f996: number = 0;
    f997: number = 0;
    f998: number = 0;
    f999: number = 0;
    f1000: number = 0;
    f1001: number = 0;
    f1002: number = 0;
    f1003: number = 0;
    f1004: number = 0;
    f1005: number = 0;
    f1006: number = 0;
    f1007: number = 0;
    f1008: number = 0;
    f1009: number = 0;
    f1010: number = 0;
    f1011: number = 0;
    f1012: number = 0;
    f1013: number = 0;
    f1014: number = 0;
    f1015: number = 0;
    f1016: number = 0;
    f1017: number = 0;
    f1018: number = 0;
    f1019: number = 0;
    f1020: number = 0;
    f1021: number = 0;
    f1022: number = 0;
    f1023: number = 0;
    f1024: number = 0;
    f1025: number = 0;
    f1026: number = 0;
    f1027: number = 0;
    f1028: number = 0;
    f1029: number = 0;
    constructor() { "use sendable"; }
}

print("=== G13 assign element-dict order (D1 compat) ===");
// D1a: full element assignment order via recording setters (C++ sorts element keys ascending)
(function () {
    let src = new SrcElem();
    let order: number[] = [];
    let t: any = {};
    let indices: number[] = [3, 8, 21, 42, 77, 99, 150, 300];
    for (let i = 0; i < indices.length; i++) {
        let k = "" + indices[i];
        Object.defineProperty(t, k, {
            set: function (v) { order.push(v); },
            enumerable: true,
            configurable: true
        });
    }
    Object.assign(t, src);
    print("G13a " + order.join(","));
})();
// D1b: exception position and partial state (throw at "42", ascending order stops before it)
(function () {
    let src = new SrcElem();
    let t: any = {};
    Object.defineProperty(t, "42", { value: -1, writable: false, enumerable: true, configurable: false });
    try {
        Object.assign(t, src);
        print("G13b NOT_THROW");
    } catch (e) {
        let ok = (t[3] === 3) && (t[8] === 8) && (t[21] === 21) && (t[42] === -1) &&
            (t[77] === undefined) && (t[99] === undefined) && (t[150] === undefined) && (t[300] === undefined);
        print("G13b " + e.name + "," + ok);
    }
})();
// D1c/D1d: mixed elements + named fields — elements ascending first, then named in declaration order
(function () {
    let src = new SrcMix();
    let t: any = {};
    Object.assign(t, src);
    print("G13c " + t[3] + "," + t[77] + "," + t.name + "," + t.tag);
    let log: string[] = [];
    let t2: any = {};
    function watch(obj: any, k: string) {
        Object.defineProperty(obj, k, {
            set: function (v) { log.push(k); },
            enumerable: true,
            configurable: true
        });
    }
    watch(t2, "3");
    watch(t2, "77");
    watch(t2, "name");
    watch(t2, "tag");
    Object.assign(t2, src);
    print("G13d " + log.join(","));
})();

print("=== G14 assign prop-dict order (D2 compat) ===");
// D2a: dict-mode source (1030 fields > MAX_INLINE) — full copy
(function () {
    let src = new DictSrc();
    let t: any = {};
    Object.assign(t, src);
    let cnt = 0;
    for (let i = 0; i < 1030; i++) {
        if (t["f" + i] === 0) { cnt++; }
    }
    print("G14a " + cnt + "," + Object.keys(t).length);
})();
// D2b: exception position and partial state (throw at f500, declaration order stops before it)
(function () {
    let src = new DictSrc();
    let t: any = {};
    Object.defineProperty(t, "f500", { value: -1, writable: false, enumerable: true, configurable: false });
    try {
        Object.assign(t, src);
        print("G14b NOT_THROW");
    } catch (e) {
        let ok = true;
        for (let i = 0; i < 500; i++) {
            if (t["f" + i] !== 0) { ok = false; break; }
        }
        for (let i = 501; i < 1030; i++) {
            if (t["f" + i] !== undefined) { ok = false; break; }
        }
        print("G14b " + e.name + "," + ok + "," + (t.f500 === -1));
    }
})();

print("=== G11 shared async function ===");
print(typeof sfAsync.call);
print(typeof sfAsync.apply);
print(typeof sfAsync.bind);
print(typeof sfAsync.then);
print(sfAsync instanceof Function);
sfAsync(21).then(function (r: number) { print("async result: " + r); });

print("=== end ===");
