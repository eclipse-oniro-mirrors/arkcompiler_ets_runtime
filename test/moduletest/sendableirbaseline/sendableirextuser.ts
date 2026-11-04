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
 * @tc.name:sendableirextuser
 * @tc.desc:LDSENDABLEEXTERNALMODULEVAR imm8/wide benches
 * @tc.type: FUNC
 */

import {
    mv000, mv001, mv002, mv003, mv004, mv005, mv006, mv007, mv008, mv009, mv010, mv011, mv012, mv013,
    mv014, mv015, mv016, mv017, mv018, mv019, mv020, mv021, mv022, mv023, mv024, mv025, mv026, mv027,
    mv028, mv029, mv030, mv031, mv032, mv033, mv034, mv035, mv036, mv037, mv038, mv039, mv040, mv041,
    mv042, mv043, mv044, mv045, mv046, mv047, mv048, mv049, mv050, mv051, mv052, mv053, mv054, mv055,
    mv056, mv057, mv058, mv059, mv060, mv061, mv062, mv063, mv064, mv065, mv066, mv067, mv068, mv069,
    mv070, mv071, mv072, mv073, mv074, mv075, mv076, mv077, mv078, mv079, mv080, mv081, mv082, mv083,
    mv084, mv085, mv086, mv087, mv088, mv089, mv090, mv091, mv092, mv093, mv094, mv095, mv096, mv097,
    mv098, mv099, mv100, mv101, mv102, mv103, mv104, mv105, mv106, mv107, mv108, mv109, mv110, mv111,
    mv112, mv113, mv114, mv115, mv116, mv117, mv118, mv119, mv120, mv121, mv122, mv123, mv124, mv125,
    mv126, mv127, mv128, mv129, mv130, mv131, mv132, mv133, mv134, mv135, mv136, mv137, mv138, mv139,
    mv140, mv141, mv142, mv143, mv144, mv145, mv146, mv147, mv148, mv149, mv150, mv151, mv152, mv153,
    mv154, mv155, mv156, mv157, mv158, mv159, mv160, mv161, mv162, mv163, mv164, mv165, mv166, mv167,
    mv168, mv169, mv170, mv171, mv172, mv173, mv174, mv175, mv176, mv177, mv178, mv179, mv180, mv181,
    mv182, mv183, mv184, mv185, mv186, mv187, mv188, mv189, mv190, mv191, mv192, mv193, mv194, mv195,
    mv196, mv197, mv198, mv199, mv200, mv201, mv202, mv203, mv204, mv205, mv206, mv207, mv208, mv209,
    mv210, mv211, mv212, mv213, mv214, mv215, mv216, mv217, mv218, mv219, mv220, mv221, mv222, mv223,
    mv224, mv225, mv226, mv227, mv228, mv229, mv230, mv231, mv232, mv233, mv234, mv235, mv236, mv237,
    mv238, mv239, mv240, mv241, mv242, mv243, mv244, mv245, mv246, mv247, mv248, mv249, mv250, mv251,
    mv252, mv253, mv254, mv255, mv256, mv257, mv258, mv259, mv260, mv261, mv262, mv263, mv264, mv265,
    mv266, mv267, mv268, mv269, mv270, mv271, mv272, mv273, mv274, mv275, mv276, mv277, mv278, mv279,
    mv280, mv281, mv282, mv283, mv284, mv285, mv286, mv287, mv288, mv289, mv290, mv291, mv292, mv293,
    mv294, mv295, mv296, mv297, mv298, mv299,
} from "./sendableirexport";

// selector 0: LDSENDABLEEXTERNALMODULEVAR (import index 20, fits imm8)
// selector 1: WIDELDSENDABLEEXTERNALMODULEVAR (import index 299, needs imm16)
export function benchLdExtModuleVar(selector: number, n: number): number {
    "use sendable";
    let r = 0;
    if (selector === 0) {
        for (let i = 0; i < n; i++) {
            r += mv020;
            r += mv020;
            r += mv020;
            r += mv020;
            r += mv020;
            r += mv020;
            r += mv020;
            r += mv020;
            r += mv020;
            r += mv020;
        }
    } else {
        for (let i = 0; i < n; i++) {
            r += mv299;
            r += mv299;
            r += mv299;
            r += mv299;
            r += mv299;
            r += mv299;
            r += mv299;
            r += mv299;
            r += mv299;
            r += mv299;
        }
    }
    return r;
}
