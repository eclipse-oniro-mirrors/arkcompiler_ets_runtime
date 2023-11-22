/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_PGO_PROFILER_LAYOUT_H
#define ECMASCRIPT_PGO_PROFILER_LAYOUT_H

#include <cstdint>
#include <string>

#include "ecmascript/ecma_vm.h"
#include "ecmascript/elements.h"
#include "ecmascript/js_hclass.h"
#include "ecmascript/js_object.h"
#include "ecmascript/log_wrapper.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/mem/region.h"
#include "ecmascript/pgo_profiler/pgo_context.h"
#include "ecmascript/pgo_profiler/pgo_utils.h"
#include "ecmascript/pgo_profiler/types/pgo_profile_type.h"
#include "ecmascript/pgo_profiler/types/pgo_profiler_type.h"
#include "ecmascript/property_attributes.h"

namespace panda::ecmascript::pgo {
class PGOHandler {
public:
    using PropertyMetaDataField = BitField<int, 0, 4>;  // 4: property metaData field occupies 4 bits
    using WritableField = BitField<bool, 0, 1>;
    using EnumerableField = WritableField::NextFlag;
    using ConfigurableField = EnumerableField::NextFlag;
    using IsAccessorField = ConfigurableField::NextFlag;
    using TrackTypeField = IsAccessorField::NextField<TrackType, PropertyAttributes::TRACK_TYPE_NUM>;

    PGOHandler()
    {
        SetTrackType(TrackType::NONE);
        SetPropertyMeta(false);
    }

    PGOHandler(TrackType type, int meta)
    {
        SetTrackType(type);
        SetPropertyMeta(meta);
    }

    uint32_t GetValue() const
    {
        return value_;
    }

    bool SetAttribute(const JSThread *thread, PropertyAttributes &attr) const
    {
        bool ret = false;
        if (thread->GetEcmaVM()->GetJSOptions().IsEnableOptTrackField()) {
            switch (GetTrackType()) {
                case TrackType::DOUBLE:
                case TrackType::NUMBER:
                    attr.SetRepresentation(Representation::DOUBLE);
                    ret = true;
                    break;
                case TrackType::INT:
                    attr.SetRepresentation(Representation::INT);
                    ret = true;
                    break;
                case TrackType::TAGGED:
                default:
                    attr.SetRepresentation(Representation::TAGGED);
                    break;
            }
        } else {
            attr.SetRepresentation(Representation::TAGGED);
        }
        attr.SetWritable(IsWritable());
        attr.SetEnumerable(IsEnumerable());
        attr.SetConfigurable(IsConfigurable());
        attr.SetIsAccessor(IsAccessor());
        return ret;
    }

    void SetTrackType(TrackType type)
    {
        TrackTypeField::Set(type, &value_);
    }

    TrackType GetTrackType() const
    {
        return TrackTypeField::Get(value_);
    }

    void SetPropertyMeta(int meta)
    {
        PropertyMetaDataField::Set(meta, &value_);
    }

    int GetPropertyMeta() const
    {
        return PropertyMetaDataField::Get(value_);
    }

    bool IsAccessor() const
    {
        return IsAccessorField::Get(value_);
    }

    bool IsWritable() const
    {
        return WritableField::Get(value_);
    }

    bool IsEnumerable() const
    {
        return EnumerableField::Get(value_);
    }

    bool IsConfigurable() const
    {
        return ConfigurableField::Get(value_);
    }

    bool operator!=(const PGOHandler &right) const
    {
        return value_ != right.value_;
    }

    bool operator==(const PGOHandler &right) const
    {
        return value_ == right.value_;
    }

private:
    uint32_t value_ { 0 };
};

using PropertyDesc = std::pair<CString, PGOHandler>;
using LayoutDesc = CVector<PropertyDesc>;
class PGOHClassTreeDesc;

struct ElementsTrackInfo {
    std::string ToString() const
    {
        std::stringstream stream;
            stream << "(size: " << arrayLength_ << ", " << ToSpaceTypeName(spaceFlag_) << ")";
        return stream.str();
    }

    uint32_t arrayLength_ { 0 };
    RegionSpaceFlag spaceFlag_ { RegionSpaceFlag::UNINITIALIZED };
};

class HClassLayoutDesc {
public:
    explicit HClassLayoutDesc(ProfileType type) : type_(type) {}
    HClassLayoutDesc(ProfileType type, const CSet<ProfileType> &childs) : type_(type), childs_(childs) {}
    virtual ~HClassLayoutDesc() = default;

    virtual void Merge(const HClassLayoutDesc *from);
    virtual void InsertKeyAndDesc(const CString &key, const PGOHandler &handler) = 0;
    virtual bool UpdateKeyAndDesc(const CString &key, const PGOHandler &handler) = 0;

    ProfileType GetProfileType() const
    {
        return type_;
    }

    void AddChildHClassLayoutDesc(const ProfileType &type)
    {
        childs_.emplace(type);
    }

    inline size_t GetChildSize() const
    {
        return childs_.size();
    }

    template<typename Callback>
    void IterateChilds(Callback callback) const
    {
        for (const auto &type : childs_) {
            if (!callback(type)) {
                break;
            }
        }
    }

protected:
    void InsertKeyAndDesc(const PGOHandler &handler, PropertyDesc &desc);

    ProfileType type_;
    CSet<ProfileType> childs_;
};

class RootHClassLayoutDesc final : public HClassLayoutDesc {
public:
    explicit RootHClassLayoutDesc(ProfileType type) : HClassLayoutDesc(type) {}
    RootHClassLayoutDesc(ProfileType type, JSType objType, uint32_t objSize)
        : HClassLayoutDesc(type), objType_(objType), objSize_(objSize) {}
    RootHClassLayoutDesc(const RootHClassLayoutDesc &desc)
        : HClassLayoutDesc(desc.type_, desc.childs_), objType_(desc.objType_), objSize_(desc.objSize_),
          layoutDesc_(desc.layoutDesc_) {}
    RootHClassLayoutDesc& operator=(const RootHClassLayoutDesc &desc)
    {
        this->type_ = desc.type_;
        this->childs_ = desc.childs_;
        this->objType_ = desc.objType_;
        this->objSize_ = desc.objSize_;
        this->layoutDesc_ = desc.layoutDesc_;
        return *this;
    }

    void Merge(const HClassLayoutDesc *from) override;
    void InsertKeyAndDesc(const CString &key, const PGOHandler &handler) override;
    bool UpdateKeyAndDesc(const CString &key, const PGOHandler &handler) override;

    void SetObjectSize(uint32_t objSize)
    {
        objSize_ = objSize;
    }

    uint32_t GetObjectSize() const
    {
        return objSize_;
    }

    void SetObjectType(JSType objType)
    {
        objType_ = objType;
    }

    JSType GetObjectType() const
    {
        return objType_;
    }

    size_t NumOfProps() const
    {
        return layoutDesc_.size();
    }

    template<typename Callback>
    void IterateProps(Callback callback) const
    {
        for (const auto &iter : layoutDesc_) {
            callback(iter);
        }
    }

private:
    JSType objType_;
    uint32_t objSize_;
    LayoutDesc layoutDesc_;
};

class ChildHClassLayoutDesc final : public HClassLayoutDesc {
public:
    explicit ChildHClassLayoutDesc(ProfileType type) : HClassLayoutDesc(type) {}
    ChildHClassLayoutDesc(const ChildHClassLayoutDesc &desc)
        : HClassLayoutDesc(desc.type_, desc.childs_), propertyDesc_(desc.propertyDesc_) {}
    ChildHClassLayoutDesc& operator=(const ChildHClassLayoutDesc &desc)
    {
        this->type_ = desc.type_;
        this->childs_ = desc.childs_;
        this->propertyDesc_ = desc.propertyDesc_;
        return *this;
    }

    void Merge(const HClassLayoutDesc *from) override;
    void InsertKeyAndDesc(const CString &key, const PGOHandler &handler) override;
    bool UpdateKeyAndDesc(const CString &key, const PGOHandler &handler) override;

    PropertyDesc GetPropertyDesc() const
    {
        return propertyDesc_;
    }

private:
    PropertyDesc propertyDesc_;
};

class PGOHClassTreeDesc {
public:
    PGOHClassTreeDesc() = default;
    explicit PGOHClassTreeDesc(ProfileType type) : type_(type) {}

    void Clear();

    ProfileType GetProfileType() const
    {
        return type_;
    }

    void Merge(const PGOHClassTreeDesc &from);

    bool operator<(const PGOHClassTreeDesc &right) const
    {
        return type_ < right.type_;
    }

    ElementsTrackInfo GetElementsTrackInfo() const
    {
        return elementTrackInfo_;
    }

    uint32_t GetArrayLength() const
    {
        return elementTrackInfo_.arrayLength_;
    }

    void UpdateArrayLength(uint32_t size)
    {
        elementTrackInfo_.arrayLength_ = size;
    }

    RegionSpaceFlag GetSpaceFlag() const
    {
        return elementTrackInfo_.spaceFlag_;
    }

    void UpdateSpaceFlag(RegionSpaceFlag spaceFlag)
    {
        elementTrackInfo_.spaceFlag_ = spaceFlag;
    }

    HClassLayoutDesc *GetHClassLayoutDesc(ProfileType type) const;
    HClassLayoutDesc *GetOrInsertHClassLayoutDesc(ProfileType type, bool root = false);

    bool DumpForRoot(JSTaggedType root, ProfileType rootType);
    bool DumpForChild(JSTaggedType child, ProfileType childType);
    bool UpdateForChild(ProfileType rootType, JSTaggedType child, ProfileType childType);
    bool DumpForTransition(JSTaggedType parent, ProfileType parentType, JSTaggedType child, ProfileType childType);
    bool UpdateLayout(ProfileType rootType, JSTaggedType curHClass, ProfileType curType);

    template<typename Callback>
    void IterateChilds(Callback callback) const
    {
        IterateAll([&callback] (HClassLayoutDesc *desc) {
            if (desc->GetProfileType().IsRootType()) {
                return;
            }
            callback(reinterpret_cast<ChildHClassLayoutDesc *>(desc));
        });
    }

private:
    template<typename Callback>
    void IterateAll(Callback callback) const
    {
        for (auto iter : transitionLayout_) {
            callback(iter.second);
        }
    }

    ProfileType type_;
    ElementsTrackInfo elementTrackInfo_;
    CMap<ProfileType, HClassLayoutDesc *> transitionLayout_;
};

class PGOLayoutDescInfo {
public:
    PGOLayoutDescInfo() = default;
    PGOLayoutDescInfo(const CString &key, PGOHandler handler) : handler_(handler)
    {
        size_t len = key.size();
        size_ = Size(len);
        if (len > 0 && memcpy_s(&key_, len, key.c_str(), len) != EOK) {
            LOG_ECMA(ERROR) << "SetMethodName memcpy_s failed" << key << ", len = " << len;
            UNREACHABLE();
        }
        *(&key_ + len) = '\0';
    }

    static int32_t Size(size_t len)
    {
        return sizeof(PGOLayoutDescInfo) + AlignUp(len, GetAlignmentInBytes(ALIGN_SIZE));
    }

    int32_t Size() const
    {
        return size_;
    }

    const char *GetKey() const
    {
        return &key_;
    }

    PGOHandler GetHandler() const
    {
        return handler_;
    }

private:
    int32_t size_ {0};
    PGOHandler handler_;
    char key_ {'\0'};
};

class HClassLayoutDescInner {
public:
    virtual ~HClassLayoutDescInner() = default;
    int32_t Size() const
    {
        return size_;
    }

    ProfileType GetProfileType() const
    {
        return type_;
    }

protected:
    int32_t size_ { 0 };
    ProfileType type_;
};

class RootHClassLayoutDescInner : public HClassLayoutDescInner {
public:
    static size_t CaculateSize(const RootHClassLayoutDesc &desc)
    {
        size_t size = sizeof(RootHClassLayoutDescInner);
        size += desc.GetChildSize() * sizeof(ProfileType);
        desc.IterateProps([&size] (const PropertyDesc &propDesc) {
            auto key = propDesc.first;
            size += static_cast<size_t>(PGOLayoutDescInfo::Size(key.size()));
        });
        return size;
    }

    void Merge(const RootHClassLayoutDesc &desc)
    {
        size_ = static_cast<int32_t>(RootHClassLayoutDescInner::CaculateSize(desc));
        type_ = desc.GetProfileType();
        childCount_ = 0;
        desc.IterateChilds([this] (const ProfileType &childType) -> bool {
            auto newChildType = const_cast<ProfileType *>(GetChildType(childCount_++));
            new (newChildType) ProfileType(childType);
            return true;
        });

        auto current = const_cast<PGOLayoutDescInfo *>(GetFirstProperty());
        desc.IterateProps([this, &current] (const PropertyDesc &propDesc) {
            auto key = propDesc.first;
            auto type = propDesc.second;
            new (current) PGOLayoutDescInfo(key, type);
            current = const_cast<PGOLayoutDescInfo *>(GetNextProperty(current));
            propCount_++;
        });
    }

    void Convert(PGOContext& context, RootHClassLayoutDesc *desc) const
    {
        auto descInfo = GetFirstProperty();
        for (uint32_t i = 0; i < propCount_; i++) {
            desc->InsertKeyAndDesc(descInfo->GetKey(), descInfo->GetHandler());
            descInfo = GetNextProperty(descInfo);
        }
        for (uint32_t i = 0; i < childCount_; i++) {
            auto profileType(*GetChildType(i));
            desc->AddChildHClassLayoutDesc(profileType.Remap(context));
        }
    }

    void SetObjectType(JSType objType)
    {
        objType_ = objType;
    }

    JSType GetObjectType() const
    {
        return objType_;
    }

    void SetObjectSize(uint32_t size)
    {
        objSize_ = size;
    }

    uint32_t GetObjectSize() const
    {
        return objSize_;
    }

private:
    const ProfileType *GetChildType(uint32_t index) const
    {
        return (&childType_) + index;
    }

    const PGOLayoutDescInfo *GetFirstProperty() const
    {
        return reinterpret_cast<const PGOLayoutDescInfo *>(reinterpret_cast<uintptr_t>(&childType_ + childCount_));
    }

    const PGOLayoutDescInfo *GetNextProperty(const PGOLayoutDescInfo *current) const
    {
        return reinterpret_cast<const PGOLayoutDescInfo *>(reinterpret_cast<uintptr_t>(current) + current->Size());
    }

    JSType objType_;
    uint32_t objSize_;
    uint32_t propCount_ {0};
    uint32_t childCount_ {0};
    ProfileType childType_;
};

class ChildHClassLayoutDescInner : public HClassLayoutDescInner {
public:
    static size_t CaculateSize(const ChildHClassLayoutDesc &desc)
    {
        size_t size = sizeof(ChildHClassLayoutDescInner);
        size += desc.GetChildSize() * sizeof(ProfileType);
        auto key = desc.GetPropertyDesc().first;
        size += static_cast<size_t>(PGOLayoutDescInfo::Size(key.size()));
        return size;
    }

    void Merge(const ChildHClassLayoutDesc &desc)
    {
        size_ = ChildHClassLayoutDescInner::CaculateSize(desc);
        type_ = desc.GetProfileType();
        childCount_ = 0;
        desc.IterateChilds([this] (const ProfileType &childType) -> bool {
            auto newChildType = const_cast<ProfileType *>(GetChildType(childCount_++));
            new (newChildType) ProfileType(childType);
            return true;
        });

        auto current = const_cast<PGOLayoutDescInfo *>(GetProperty());
        auto propDesc = desc.GetPropertyDesc();
        auto key = propDesc.first;
        auto type = propDesc.second;
        new (current) PGOLayoutDescInfo(key, type);
    }

    void Convert(PGOContext& context, ChildHClassLayoutDesc *desc) const
    {
        auto descInfo = GetProperty();
        desc->InsertKeyAndDesc(descInfo->GetKey(), descInfo->GetHandler());
        for (uint32_t i = 0; i < childCount_; i++) {
            auto profileType(*GetChildType(i));
            desc->AddChildHClassLayoutDesc(profileType.Remap(context));
        }
    }

private:
    const ProfileType *GetChildType(uint32_t index) const
    {
        return (&childType_) + index;
    }

    const PGOLayoutDescInfo *GetProperty() const
    {
        return reinterpret_cast<const PGOLayoutDescInfo *>(&childType_ + childCount_);
    }

    uint32_t childCount_ { 0 };
    ProfileType childType_;
};

template <typename SampleType>
class PGOHClassTreeTemplate {
public:
    PGOHClassTreeTemplate(size_t size, SampleType type, ElementsTrackInfo &trackInfo)
        : size_(size), type_(type)
    {
        SetElementsTrackInfo(trackInfo);
    }

    static size_t CaculateSize(const PGOHClassTreeDesc &desc)
    {
        auto rootLayout = desc.GetHClassLayoutDesc(desc.GetProfileType());
        if (rootLayout == nullptr) {
            return sizeof(PGOHClassTreeTemplate<SampleType>);
        }
        size_t size = sizeof(PGOHClassTreeTemplate<SampleType>) - sizeof(RootHClassLayoutDescInner);
        size += RootHClassLayoutDescInner::CaculateSize(*reinterpret_cast<const RootHClassLayoutDesc *>(rootLayout));

        desc.IterateChilds([&size](ChildHClassLayoutDesc *desc) {
            size += ChildHClassLayoutDescInner::CaculateSize(*desc);
        });
        size += sizeof(ElementsTrackInfo);
        return size;
    }

    static std::string GetTypeString(const PGOHClassTreeDesc &desc)
    {
        std::string text;
        text += desc.GetProfileType().GetTypeString();
        if (desc.GetArrayLength() > 0 && desc.GetSpaceFlag() != RegionSpaceFlag::UNINITIALIZED) {
            text += desc.GetElementsTrackInfo().ToString();
        }
        text += DumpUtils::BLOCK_AND_ARRAY_START;
        auto layoutDesc = desc.GetHClassLayoutDesc(desc.GetProfileType());
        if (layoutDesc != nullptr) {
            bool isLayoutFirst = true;
            ASSERT(layoutDesc->GetProfileType().IsRootType());
            auto rootLayoutDesc = reinterpret_cast<RootHClassLayoutDesc *>(layoutDesc);
            rootLayoutDesc->IterateProps([&text, &isLayoutFirst] (const PropertyDesc &propDesc) {
                if (!isLayoutFirst) {
                    text += DumpUtils::TYPE_SEPARATOR + DumpUtils::SPACE;
                } else {
                    text += DumpUtils::ARRAY_START;
                }
                isLayoutFirst = false;
                text += propDesc.first;
                text += DumpUtils::BLOCK_START;
                text += std::to_string(propDesc.second.GetValue());
            });
        }
        text += (DumpUtils::SPACE + DumpUtils::ARRAY_END);
        return text;
    }

    void Merge(const PGOHClassTreeDesc &desc)
    {
        auto root = const_cast<RootHClassLayoutDescInner *>(GetRoot());
        auto layoutDesc = desc.GetHClassLayoutDesc(desc.GetProfileType());
        if (layoutDesc == nullptr) {
            return;
        }
        auto rootLayoutDesc = reinterpret_cast<const RootHClassLayoutDesc *>(layoutDesc);
        root->Merge(*rootLayoutDesc);
        root->SetObjectType(rootLayoutDesc->GetObjectType());
        root->SetObjectSize(rootLayoutDesc->GetObjectSize());

        childCount_ = 0;
        auto last = reinterpret_cast<HClassLayoutDescInner *>(root);
        desc.IterateChilds([this, &last](ChildHClassLayoutDesc *desc) {
            auto current = const_cast<ChildHClassLayoutDescInner *>(GetNext(last));
            new (current) ChildHClassLayoutDescInner();
            current->Merge(*desc);
            last = current;
            childCount_++;
        });
    }

    PGOHClassTreeDesc Convert(PGOContext& context)
    {
        PGOHClassTreeDesc desc(ProfileType(context, GetType().GetProfileType()));
        auto root = GetRoot();
        if (root->GetProfileType().IsNone()) {
            return desc;
        }
        auto layoutDesc = desc.GetOrInsertHClassLayoutDesc(root->GetProfileType().Remap(context), true);
        auto rootLayoutDesc = reinterpret_cast<RootHClassLayoutDesc *>(layoutDesc);
        rootLayoutDesc->SetObjectType(root->GetObjectType());
        rootLayoutDesc->SetObjectSize(root->GetObjectSize());
        root->Convert(context, rootLayoutDesc);

        auto last = reinterpret_cast<const HClassLayoutDescInner *>(root);
        for (int32_t i = 0; i < childCount_; i++) {
            auto current = GetNext(last);
            auto childLayoutDesc = desc.GetOrInsertHClassLayoutDesc(current->GetProfileType().Remap(context), false);
            current->Convert(context, reinterpret_cast<ChildHClassLayoutDesc *>(childLayoutDesc));
            last = current;
        }
        if (context.SupportElementsTrackInfo()) {
            auto trackInfo = GetElementsTrackInfo();
            desc.UpdateArrayLength(trackInfo->arrayLength_);
            desc.UpdateSpaceFlag(trackInfo->spaceFlag_);
        }
        return desc;
    }

    int32_t Size() const
    {
        return size_;
    }

    SampleType GetType() const
    {
        return type_;
    }

private:
    const RootHClassLayoutDescInner *GetRoot() const
    {
        return &rootHClassLayout_;
    }

    const ChildHClassLayoutDescInner *GetNext(const HClassLayoutDescInner *current) const
    {
        return reinterpret_cast<const ChildHClassLayoutDescInner *>(
            reinterpret_cast<uintptr_t>(current) + current->Size());
    }

    void SetElementsTrackInfo(ElementsTrackInfo trackInfo)
    {
        auto trackInfoOffset = GetEnd() - sizeof(ElementsTrackInfo);
        *reinterpret_cast<ElementsTrackInfo *>(trackInfoOffset) = trackInfo;
    }

    ElementsTrackInfo* GetElementsTrackInfo()
    {
        auto trackInfoOffset = GetEnd() - sizeof(ElementsTrackInfo);
        return reinterpret_cast<ElementsTrackInfo *>(trackInfoOffset);
    }

    uintptr_t GetEnd() const
    {
        return reinterpret_cast<uintptr_t>(this) + Size();
    }

    int32_t size_;
    SampleType type_;
    int32_t childCount_ { 0 };
    RootHClassLayoutDescInner rootHClassLayout_;
};

using PGOHClassTreeDescInner = PGOHClassTreeTemplate<PGOSampleType>;
using PGOHClassTreeDescInnerRef = PGOHClassTreeTemplate<PGOSampleTypeRef>;
} // namespace panda::ecmascript::pgo
#endif // ECMASCRIPT_PGO_PROFILER_LAYOUT_H
