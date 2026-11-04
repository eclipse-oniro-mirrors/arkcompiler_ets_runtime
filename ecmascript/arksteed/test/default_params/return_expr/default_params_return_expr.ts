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
// G11.6 additive evidence (S11/C1/C3/C4): the compiled target performs no JS
// call, so the actual invocation form of this leaf is carried by its own
// silent invocation carrier `callDefaultParamReturnExpr`, whose dump holds the
// generic `Call` of the omitted, partially supplied and fully supplied calls.
// The original target keeps its own positive compile/entry evidence, the
// probes and the output bytes are unchanged, and the normal path must not
// deopt. The `_GE` lower bound allows the installation and recompilation
// multiplicity of the same method.
//! METHOD callDefaultParamReturnExpr
//! HAS Call
//! COMPILE_COUNT_GE callDefaultParamReturnExpr 1
//! EXEC_COUNT_GE *callDefaultParamReturnExpr* 1
//! METHOD default_param_return_expr
//! COMPILE_COUNT_GE default_param_return_expr 1
//! EXEC_COUNT_GE *default_param_return_expr* 1
//! DEOPT_COUNT *Deoptimize* 0

function default_param_return_expr(a: number = 1, b: number = 2, c: number = 3): number {
    let left = a + b;
    let right = b + c;
    return left * right;
}

function callDefaultParamReturnExpr(): number {
    return default_param_return_expr()
        + default_param_return_expr(4)
        + default_param_return_expr(4, 5, 6);
}

ArkTools.arkSteedCompileSync(default_param_return_expr);
ArkTools.arkSteedCompileSync(callDefaultParamReturnExpr);


print(default_param_return_expr());
print(default_param_return_expr(4));
print(default_param_return_expr(4, 5, 6));
callDefaultParamReturnExpr();
