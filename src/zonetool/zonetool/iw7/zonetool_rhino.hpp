#pragma once
// Serializer extension for pinned Joelrau/x64-zt IW7 layouts (GPL-3.0 upstream).
// Asset data is read from the owner's local installation and stays outside Git.
namespace zonetool::iw7
{
    class rhino_anim_class final : public asset_interface
    {
        std::string name_;
        AnimationClass* asset_{};
        std::map<scr_string_t*, const char*> strings_;

        template<class T> static T* clone(T* source, size_t count, zone_memory* memory)
        {
            if (!source) return nullptr;
            auto* result = memory->allocate<T>(count);
            std::memcpy(result, source, sizeof(T) * count);
            return result;
        }
        void strings(scr_string_t* source, size_t count)
        {
            if (source) for (size_t i = 0; i < count; ++i)
                strings_[source + i] = source[i] ? SL_ConvertToString(source[i]) : nullptr;
        }
        template<class T> static void array(zone_buffer* buffer, T* source, T** destination, size_t count, int alignment = 3)
        {
            if (!source) return;
            buffer->align(alignment);
            buffer->write(source, count);
            buffer->clear_pointer(destination);
        }
        static void runtime_array(zone_buffer* buffer, unsigned __int64* source, unsigned __int64** destination, size_t count)
        {
            buffer->push_stream(XFILE_BLOCK_RUNTIME);
            array(buffer, source, destination, count, 7);
            buffer->pop_stream();
        }
    public:
        void init(const std::string& name, zone_memory* memory) override
        {
            if (name != "alien_rhino_animclass") throw std::runtime_error("Rhino serializer accepts only alien_rhino_animclass");
            name_ = name;
            asset_ = clone(db_find_x_asset_header_safe(ASSET_TYPE_ANIMCLASS, name).animClass, 1, memory);
            if (!asset_ || !asset_->stateMachine) throw std::runtime_error("Rhino animation class/state machine missing");
            asset_->stateMachine = clone(asset_->stateMachine, 1, memory);
            auto* machine = asset_->stateMachine;
            if (machine->stateCount > 2048 || machine->aimSetCount > 1024 || asset_->soundCount > 4096 || asset_->effectCount > 4096)
                throw std::runtime_error("Rhino animation class counts out of bounds");
            strings(&machine->name, 1);
            strings(&asset_->animTree, 1);
            machine->states = clone(machine->states, machine->stateCount, memory);
            for (size_t i = 0; i < machine->stateCount; ++i)
            {
                auto& state = machine->states[i];
                strings(&state.name, 1); strings(&state.notify, 1);
                const auto entries = static_cast<unsigned char>(state.entryCount);
                state.animEntries = clone(state.animEntries, entries, memory);
                for (size_t j = 0; j < entries; ++j) strings(&state.animEntries[j].animName, 1);
                state.aliasList = clone(state.aliasList, static_cast<unsigned char>(state.aliasCount), memory);
                for (size_t j = 0; j < static_cast<unsigned char>(state.aliasCount); ++j)
                {
                    strings(&state.aliasList[j].aliasName, 1);
                    state.aliasList[j].aliasInfo = clone(state.aliasList[j].aliasInfo, static_cast<unsigned char>(state.aliasList[j].animCount), memory);
                }
            }
            machine->aimSets = clone(machine->aimSets, machine->aimSetCount, memory);
            for (size_t i = 0; i < machine->aimSetCount; ++i)
            {
                auto& aim = machine->aimSets[i];
                if (aim.animCount < 0 || aim.animCount > 4096) throw std::runtime_error("Rhino aim animation count out of bounds");
                strings(&aim.name, 1); strings(&aim.rootName, 1);
                aim.animName = clone(aim.animName, aim.animCount, memory);
                strings(aim.animName, aim.animCount);
            }
#define RHINO_STRINGS(field, count) asset_->field = clone(asset_->field, asset_->count, memory); strings(asset_->field, asset_->count)
            RHINO_STRINGS(soundNotes, soundCount); RHINO_STRINGS(soundNames, soundCount); RHINO_STRINGS(soundOptions, soundCount);
            RHINO_STRINGS(effectNotes, effectCount); RHINO_STRINGS(effectTags, effectCount);
#undef RHINO_STRINGS
            asset_->effectDefs = clone(asset_->effectDefs, asset_->effectCount, memory);
        }
        void prepare(zone_buffer* buffer, zone_memory*) override
        {
            for (const auto& [pointer, value] : strings_) *pointer = static_cast<scr_string_t>(buffer->write_scriptstring(value));
        }
        void load_depending(zone_base* zone) override
        {
            const auto* tree = strings_.at(&asset_->animTree);
            if (!tree || !*tree) throw std::runtime_error("Rhino animation tree name missing");
            auto tree_file = std::string(tree);
            if (!tree_file.starts_with("animtrees/")) tree_file = "animtrees/" + tree_file;
            if (!tree_file.ends_with(".atr")) tree_file += ".atr";
            if (DB_IsXAssetDefault(ASSET_TYPE_RAWFILE, tree_file.data())) throw std::runtime_error("Rhino animation tree rawfile missing");
            rawfile::dump(db_find_x_asset_header_safe(ASSET_TYPE_RAWFILE, tree_file).rawfile);
            zone->add_asset_of_type(ASSET_TYPE_RAWFILE, tree_file);
            auto* machine = asset_->stateMachine;
            for (size_t i = 0; i < machine->stateCount; ++i)
                for (size_t j = 0; j < static_cast<unsigned char>(machine->states[i].entryCount); ++j)
                {
                    auto* name = strings_.at(&machine->states[i].animEntries[j].animName);
                    if (name && *name) zone->add_asset_of_type(ASSET_TYPE_XANIMPARTS, name);
                }
            for (size_t i = 0; i < machine->aimSetCount; ++i)
                for (int j = 0; j < machine->aimSets[i].animCount; ++j)
                {
                    auto* name = strings_.at(machine->aimSets[i].animName + j);
                    if (name && *name) zone->add_asset_of_type(ASSET_TYPE_XANIMPARTS, name);
                }
            if (asset_->scriptable) zone->add_asset_of_type(ASSET_TYPE_SCRIPTABLE, asset_->scriptable->name);
            for (size_t i = 0; asset_->effectDefs && i < asset_->effectCount; ++i)
            {
                auto& effect = asset_->effectDefs[i];
                if (effect.u.data) zone->add_asset_of_type(effect.type == FX_COMBINED_VFX ? ASSET_TYPE_VFX : ASSET_TYPE_FX,
                    effect.type == FX_COMBINED_VFX ? effect.u.vfx->name : effect.u.fx->name);
            }
        }
        void* pointer() override { return asset_; }
        std::string name() override { return name_; }
        int type() override { return ASSET_TYPE_ANIMCLASS; }
        void write(zone_base* zone, zone_buffer* buffer) override
        {
            auto* result = buffer->write(asset_);
            buffer->push_stream(XFILE_BLOCK_VIRTUAL);
            result->className = buffer->write_str(name_);
            auto* machine = asset_->stateMachine;
            buffer->align(7);
            auto* machine_result = buffer->write(machine);
            if (machine->states)
            {
                buffer->align(7);
                auto* states = buffer->write(machine->states, machine->stateCount);
                for (size_t i = 0; i < machine->stateCount; ++i)
                {
                    auto& state = machine->states[i];
                    array(buffer, state.animEntries, &states[i].animEntries, static_cast<unsigned char>(state.entryCount));
                    runtime_array(buffer, state.animIndices, &states[i].animIndices, static_cast<unsigned char>(state.entryCount));
                    if (state.aliasList)
                    {
                        buffer->align(7);
                        auto* aliases = buffer->write(state.aliasList, static_cast<unsigned char>(state.aliasCount));
                        for (size_t j = 0; j < static_cast<unsigned char>(state.aliasCount); ++j)
                            array(buffer, state.aliasList[j].aliasInfo, &aliases[j].aliasInfo, static_cast<unsigned char>(state.aliasList[j].animCount));
                        buffer->clear_pointer(&states[i].aliasList);
                    }
                }
                buffer->clear_pointer(&machine_result->states);
            }
            if (machine->aimSets)
            {
                buffer->align(7);
                auto* aims = buffer->write(machine->aimSets, machine->aimSetCount);
                for (size_t i = 0; i < machine->aimSetCount; ++i)
                {
                    auto& aim = machine->aimSets[i];
                    array(buffer, aim.animName, &aims[i].animName, aim.animCount);
                    runtime_array(buffer, aim.animIndices, &aims[i].animIndices, aim.animCount);
                    runtime_array(buffer, aim.aimNodeIndices, &aims[i].aimNodeIndices, aim.animCount);
                }
                buffer->clear_pointer(&machine_result->aimSets);
            }
            buffer->clear_pointer(&result->stateMachine);
            if (asset_->scriptable) result->scriptable = reinterpret_cast<ScriptableDef*>(zone->get_asset_pointer(ASSET_TYPE_SCRIPTABLE, asset_->scriptable->name));
            array(buffer, asset_->soundNotes, &result->soundNotes, asset_->soundCount);
            array(buffer, asset_->soundNames, &result->soundNames, asset_->soundCount);
            array(buffer, asset_->soundOptions, &result->soundOptions, asset_->soundCount);
            array(buffer, asset_->effectNotes, &result->effectNotes, asset_->effectCount);
            if (asset_->effectDefs)
            {
                buffer->align(7);
                auto* effects = buffer->write(asset_->effectDefs, asset_->effectCount);
                for (size_t i = 0; i < asset_->effectCount; ++i)
                {
                    auto& source = asset_->effectDefs[i];
                    if (source.u.data) effects[i].u.data = zone->get_asset_pointer(source.type == FX_COMBINED_VFX ? ASSET_TYPE_VFX : ASSET_TYPE_FX,
                        source.type == FX_COMBINED_VFX ? source.u.vfx->name : source.u.fx->name);
                }
                buffer->clear_pointer(&result->effectDefs);
            }
            array(buffer, asset_->effectTags, &result->effectTags, asset_->effectCount);
            buffer->pop_stream();
        }
    };

    class rhino_behavior_tree final : public asset_interface
    {
        std::string name_;
        BehaviorTree* asset_{};
    public:
        void init(const std::string& name, zone_memory*) override
        {
            if (name != "alien_rhino") throw std::runtime_error("Rhino tree serializer accepts only alien_rhino");
            name_ = name;
            asset_ = db_find_x_asset_header_safe(ASSET_TYPE_BEHAVIOR_TREE, name).behaviorTree;
            if (!asset_ || asset_->nodeCount > 8192) throw std::runtime_error("Rhino behavior tree unavailable or invalid");
        }
        void* pointer() override { return asset_; }
        std::string name() override { return name_; }
        int type() override { return ASSET_TYPE_BEHAVIOR_TREE; }
        void write(zone_base*, zone_buffer* buffer) override
        {
            auto* result = buffer->write(asset_);
            buffer->push_stream(XFILE_BLOCK_VIRTUAL);
            result->name = buffer->write_str(name_);
            if (asset_->nodes)
            {
                buffer->align(7);
                auto* nodes = buffer->write(asset_->nodes, asset_->nodeCount);
                for (size_t i = 0; i < asset_->nodeCount; ++i)
                    if (asset_->nodes[i].name) nodes[i].name = buffer->write_str(asset_->nodes[i].name);
                buffer->clear_pointer(&result->nodes);
            }
            buffer->pop_stream();
        }
    };
}
