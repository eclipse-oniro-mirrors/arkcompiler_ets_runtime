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

// Test: inlined ldexternalmodulevar with 300 imports. The module-variable
// indexes exceed 255, so the bytecode operands need the wide prefix; every
// read must still resolve to an inline load (no runtime stub call) and the
// loaded values must be summed correctly.

declare function print(arg: string): void;

declare class ArkTools {
    static arkSteedCompileSync<T extends Function>(func: T): T;
}

//! METHOD      reader
//! HAS_NOT     CallRuntime      LdExternalModuleVarByIndexOnJSFunc
//! HAS         LoadTaggedField
//! HAS         LoadTaggedElement
//! METHOD reader
//! COMPILE_COUNT_GE *reader* 1
//! EXEC_COUNT_GE *reader* 1
//! DEOPT_COUNT *Deoptimize* 0

import { w0 as r0, w1 as r1, w2 as r2, w3 as r3, w4 as r4, w5 as r5, w6 as r6, w7 as r7, w8 as r8, w9 as r9, w10 as r10, w11 as r11, w12 as r12, w13 as r13, w14 as r14, w15 as r15, w16 as r16, w17 as r17, w18 as r18, w19 as r19, w20 as r20, w21 as r21, w22 as r22, w23 as r23, w24 as r24, w25 as r25, w26 as r26, w27 as r27, w28 as r28, w29 as r29, w30 as r30, w31 as r31, w32 as r32, w33 as r33, w34 as r34, w35 as r35, w36 as r36, w37 as r37, w38 as r38, w39 as r39, w40 as r40, w41 as r41, w42 as r42, w43 as r43, w44 as r44, w45 as r45, w46 as r46, w47 as r47, w48 as r48, w49 as r49, w50 as r50, w51 as r51, w52 as r52, w53 as r53, w54 as r54, w55 as r55, w56 as r56, w57 as r57, w58 as r58, w59 as r59, w60 as r60, w61 as r61, w62 as r62, w63 as r63, w64 as r64, w65 as r65, w66 as r66, w67 as r67, w68 as r68, w69 as r69, w70 as r70, w71 as r71, w72 as r72, w73 as r73, w74 as r74, w75 as r75, w76 as r76, w77 as r77, w78 as r78, w79 as r79, w80 as r80, w81 as r81, w82 as r82, w83 as r83, w84 as r84, w85 as r85, w86 as r86, w87 as r87, w88 as r88, w89 as r89, w90 as r90, w91 as r91, w92 as r92, w93 as r93, w94 as r94, w95 as r95, w96 as r96, w97 as r97, w98 as r98, w99 as r99, w100 as r100, w101 as r101, w102 as r102, w103 as r103, w104 as r104, w105 as r105, w106 as r106, w107 as r107, w108 as r108, w109 as r109, w110 as r110, w111 as r111, w112 as r112, w113 as r113, w114 as r114, w115 as r115, w116 as r116, w117 as r117, w118 as r118, w119 as r119, w120 as r120, w121 as r121, w122 as r122, w123 as r123, w124 as r124, w125 as r125, w126 as r126, w127 as r127, w128 as r128, w129 as r129, w130 as r130, w131 as r131, w132 as r132, w133 as r133, w134 as r134, w135 as r135, w136 as r136, w137 as r137, w138 as r138, w139 as r139, w140 as r140, w141 as r141, w142 as r142, w143 as r143, w144 as r144, w145 as r145, w146 as r146, w147 as r147, w148 as r148, w149 as r149, w150 as r150, w151 as r151, w152 as r152, w153 as r153, w154 as r154, w155 as r155, w156 as r156, w157 as r157, w158 as r158, w159 as r159, w160 as r160, w161 as r161, w162 as r162, w163 as r163, w164 as r164, w165 as r165, w166 as r166, w167 as r167, w168 as r168, w169 as r169, w170 as r170, w171 as r171, w172 as r172, w173 as r173, w174 as r174, w175 as r175, w176 as r176, w177 as r177, w178 as r178, w179 as r179, w180 as r180, w181 as r181, w182 as r182, w183 as r183, w184 as r184, w185 as r185, w186 as r186, w187 as r187, w188 as r188, w189 as r189, w190 as r190, w191 as r191, w192 as r192, w193 as r193, w194 as r194, w195 as r195, w196 as r196, w197 as r197, w198 as r198, w199 as r199, w200 as r200, w201 as r201, w202 as r202, w203 as r203, w204 as r204, w205 as r205, w206 as r206, w207 as r207, w208 as r208, w209 as r209, w210 as r210, w211 as r211, w212 as r212, w213 as r213, w214 as r214, w215 as r215, w216 as r216, w217 as r217, w218 as r218, w219 as r219, w220 as r220, w221 as r221, w222 as r222, w223 as r223, w224 as r224, w225 as r225, w226 as r226, w227 as r227, w228 as r228, w229 as r229, w230 as r230, w231 as r231, w232 as r232, w233 as r233, w234 as r234, w235 as r235, w236 as r236, w237 as r237, w238 as r238, w239 as r239, w240 as r240, w241 as r241, w242 as r242, w243 as r243, w244 as r244, w245 as r245, w246 as r246, w247 as r247, w248 as r248, w249 as r249, w250 as r250, w251 as r251, w252 as r252, w253 as r253, w254 as r254, w255 as r255, w256 as r256, w257 as r257, w258 as r258, w259 as r259, w260 as r260, w261 as r261, w262 as r262, w263 as r263, w264 as r264, w265 as r265, w266 as r266, w267 as r267, w268 as r268, w269 as r269, w270 as r270, w271 as r271, w272 as r272, w273 as r273, w274 as r274, w275 as r275, w276 as r276, w277 as r277, w278 as r278, w279 as r279, w280 as r280, w281 as r281, w282 as r282, w283 as r283, w284 as r284, w285 as r285, w286 as r286, w287 as r287, w288 as r288, w289 as r289, w290 as r290, w291 as r291, w292 as r292, w293 as r293, w294 as r294, w295 as r295, w296 as r296, w297 as r297, w298 as r298, w299 as r299 } from './ldextvar_wide.ts';

export const w0 = 0;
export const w1 = 1;
export const w2 = 2;
export const w3 = 3;
export const w4 = 4;
export const w5 = 5;
export const w6 = 6;
export const w7 = 7;
export const w8 = 8;
export const w9 = 9;
export const w10 = 10;
export const w11 = 11;
export const w12 = 12;
export const w13 = 13;
export const w14 = 14;
export const w15 = 15;
export const w16 = 16;
export const w17 = 17;
export const w18 = 18;
export const w19 = 19;
export const w20 = 20;
export const w21 = 21;
export const w22 = 22;
export const w23 = 23;
export const w24 = 24;
export const w25 = 25;
export const w26 = 26;
export const w27 = 27;
export const w28 = 28;
export const w29 = 29;
export const w30 = 30;
export const w31 = 31;
export const w32 = 32;
export const w33 = 33;
export const w34 = 34;
export const w35 = 35;
export const w36 = 36;
export const w37 = 37;
export const w38 = 38;
export const w39 = 39;
export const w40 = 40;
export const w41 = 41;
export const w42 = 42;
export const w43 = 43;
export const w44 = 44;
export const w45 = 45;
export const w46 = 46;
export const w47 = 47;
export const w48 = 48;
export const w49 = 49;
export const w50 = 50;
export const w51 = 51;
export const w52 = 52;
export const w53 = 53;
export const w54 = 54;
export const w55 = 55;
export const w56 = 56;
export const w57 = 57;
export const w58 = 58;
export const w59 = 59;
export const w60 = 60;
export const w61 = 61;
export const w62 = 62;
export const w63 = 63;
export const w64 = 64;
export const w65 = 65;
export const w66 = 66;
export const w67 = 67;
export const w68 = 68;
export const w69 = 69;
export const w70 = 70;
export const w71 = 71;
export const w72 = 72;
export const w73 = 73;
export const w74 = 74;
export const w75 = 75;
export const w76 = 76;
export const w77 = 77;
export const w78 = 78;
export const w79 = 79;
export const w80 = 80;
export const w81 = 81;
export const w82 = 82;
export const w83 = 83;
export const w84 = 84;
export const w85 = 85;
export const w86 = 86;
export const w87 = 87;
export const w88 = 88;
export const w89 = 89;
export const w90 = 90;
export const w91 = 91;
export const w92 = 92;
export const w93 = 93;
export const w94 = 94;
export const w95 = 95;
export const w96 = 96;
export const w97 = 97;
export const w98 = 98;
export const w99 = 99;
export const w100 = 100;
export const w101 = 101;
export const w102 = 102;
export const w103 = 103;
export const w104 = 104;
export const w105 = 105;
export const w106 = 106;
export const w107 = 107;
export const w108 = 108;
export const w109 = 109;
export const w110 = 110;
export const w111 = 111;
export const w112 = 112;
export const w113 = 113;
export const w114 = 114;
export const w115 = 115;
export const w116 = 116;
export const w117 = 117;
export const w118 = 118;
export const w119 = 119;
export const w120 = 120;
export const w121 = 121;
export const w122 = 122;
export const w123 = 123;
export const w124 = 124;
export const w125 = 125;
export const w126 = 126;
export const w127 = 127;
export const w128 = 128;
export const w129 = 129;
export const w130 = 130;
export const w131 = 131;
export const w132 = 132;
export const w133 = 133;
export const w134 = 134;
export const w135 = 135;
export const w136 = 136;
export const w137 = 137;
export const w138 = 138;
export const w139 = 139;
export const w140 = 140;
export const w141 = 141;
export const w142 = 142;
export const w143 = 143;
export const w144 = 144;
export const w145 = 145;
export const w146 = 146;
export const w147 = 147;
export const w148 = 148;
export const w149 = 149;
export const w150 = 150;
export const w151 = 151;
export const w152 = 152;
export const w153 = 153;
export const w154 = 154;
export const w155 = 155;
export const w156 = 156;
export const w157 = 157;
export const w158 = 158;
export const w159 = 159;
export const w160 = 160;
export const w161 = 161;
export const w162 = 162;
export const w163 = 163;
export const w164 = 164;
export const w165 = 165;
export const w166 = 166;
export const w167 = 167;
export const w168 = 168;
export const w169 = 169;
export const w170 = 170;
export const w171 = 171;
export const w172 = 172;
export const w173 = 173;
export const w174 = 174;
export const w175 = 175;
export const w176 = 176;
export const w177 = 177;
export const w178 = 178;
export const w179 = 179;
export const w180 = 180;
export const w181 = 181;
export const w182 = 182;
export const w183 = 183;
export const w184 = 184;
export const w185 = 185;
export const w186 = 186;
export const w187 = 187;
export const w188 = 188;
export const w189 = 189;
export const w190 = 190;
export const w191 = 191;
export const w192 = 192;
export const w193 = 193;
export const w194 = 194;
export const w195 = 195;
export const w196 = 196;
export const w197 = 197;
export const w198 = 198;
export const w199 = 199;
export const w200 = 200;
export const w201 = 201;
export const w202 = 202;
export const w203 = 203;
export const w204 = 204;
export const w205 = 205;
export const w206 = 206;
export const w207 = 207;
export const w208 = 208;
export const w209 = 209;
export const w210 = 210;
export const w211 = 211;
export const w212 = 212;
export const w213 = 213;
export const w214 = 214;
export const w215 = 215;
export const w216 = 216;
export const w217 = 217;
export const w218 = 218;
export const w219 = 219;
export const w220 = 220;
export const w221 = 221;
export const w222 = 222;
export const w223 = 223;
export const w224 = 224;
export const w225 = 225;
export const w226 = 226;
export const w227 = 227;
export const w228 = 228;
export const w229 = 229;
export const w230 = 230;
export const w231 = 231;
export const w232 = 232;
export const w233 = 233;
export const w234 = 234;
export const w235 = 235;
export const w236 = 236;
export const w237 = 237;
export const w238 = 238;
export const w239 = 239;
export const w240 = 240;
export const w241 = 241;
export const w242 = 242;
export const w243 = 243;
export const w244 = 244;
export const w245 = 245;
export const w246 = 246;
export const w247 = 247;
export const w248 = 248;
export const w249 = 249;
export const w250 = 250;
export const w251 = 251;
export const w252 = 252;
export const w253 = 253;
export const w254 = 254;
export const w255 = 255;
export const w256 = 256;
export const w257 = 257;
export const w258 = 258;
export const w259 = 259;
export const w260 = 260;
export const w261 = 261;
export const w262 = 262;
export const w263 = 263;
export const w264 = 264;
export const w265 = 265;
export const w266 = 266;
export const w267 = 267;
export const w268 = 268;
export const w269 = 269;
export const w270 = 270;
export const w271 = 271;
export const w272 = 272;
export const w273 = 273;
export const w274 = 274;
export const w275 = 275;
export const w276 = 276;
export const w277 = 277;
export const w278 = 278;
export const w279 = 279;
export const w280 = 280;
export const w281 = 281;
export const w282 = 282;
export const w283 = 283;
export const w284 = 284;
export const w285 = 285;
export const w286 = 286;
export const w287 = 287;
export const w288 = 288;
export const w289 = 289;
export const w290 = 290;
export const w291 = 291;
export const w292 = 292;
export const w293 = 293;
export const w294 = 294;
export const w295 = 295;
export const w296 = 296;
export const w297 = 297;
export const w298 = 298;
export const w299 = 299;

function reader(): string {
    return r0 + r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8 + r9 + r10 + r11 + r12 + r13 + r14 + r15 + r16 + r17 + r18 + r19 + r20 + r21 + r22 + r23 + r24 + r25 + r26 + r27 + r28 + r29 + r30 + r31 + r32 + r33 + r34 + r35 + r36 + r37 + r38 + r39 + r40 + r41 + r42 + r43 + r44 + r45 + r46 + r47 + r48 + r49 + r50 + r51 + r52 + r53 + r54 + r55 + r56 + r57 + r58 + r59 + r60 + r61 + r62 + r63 + r64 + r65 + r66 + r67 + r68 + r69 + r70 + r71 + r72 + r73 + r74 + r75 + r76 + r77 + r78 + r79 + r80 + r81 + r82 + r83 + r84 + r85 + r86 + r87 + r88 + r89 + r90 + r91 + r92 + r93 + r94 + r95 + r96 + r97 + r98 + r99 + r100 + r101 + r102 + r103 + r104 + r105 + r106 + r107 + r108 + r109 + r110 + r111 + r112 + r113 + r114 + r115 + r116 + r117 + r118 + r119 + r120 + r121 + r122 + r123 + r124 + r125 + r126 + r127 + r128 + r129 + r130 + r131 + r132 + r133 + r134 + r135 + r136 + r137 + r138 + r139 + r140 + r141 + r142 + r143 + r144 + r145 + r146 + r147 + r148 + r149 + r150 + r151 + r152 + r153 + r154 + r155 + r156 + r157 + r158 + r159 + r160 + r161 + r162 + r163 + r164 + r165 + r166 + r167 + r168 + r169 + r170 + r171 + r172 + r173 + r174 + r175 + r176 + r177 + r178 + r179 + r180 + r181 + r182 + r183 + r184 + r185 + r186 + r187 + r188 + r189 + r190 + r191 + r192 + r193 + r194 + r195 + r196 + r197 + r198 + r199 + r200 + r201 + r202 + r203 + r204 + r205 + r206 + r207 + r208 + r209 + r210 + r211 + r212 + r213 + r214 + r215 + r216 + r217 + r218 + r219 + r220 + r221 + r222 + r223 + r224 + r225 + r226 + r227 + r228 + r229 + r230 + r231 + r232 + r233 + r234 + r235 + r236 + r237 + r238 + r239 + r240 + r241 + r242 + r243 + r244 + r245 + r246 + r247 + r248 + r249 + r250 + r251 + r252 + r253 + r254 + r255 + r256 + r257 + r258 + r259 + r260 + r261 + r262 + r263 + r264 + r265 + r266 + r267 + r268 + r269 + r270 + r271 + r272 + r273 + r274 + r275 + r276 + r277 + r278 + r279 + r280 + r281 + r282 + r283 + r284 + r285 + r286 + r287 + r288 + r289 + r290 + r291 + r292 + r293 + r294 + r295 + r296 + r297 + r298 + r299;
}

ArkTools.arkSteedCompileSync(reader);

print(reader());
