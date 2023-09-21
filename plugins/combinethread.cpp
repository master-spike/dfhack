// combinethread - combines partially used spools of thread and sheets of cloth so that they can be
// used for weaving and make clothing jobs.

#include <vector>
#include <map>
#include <tuple>
#include <utility>
#include <algorithm>

#include "Core.h"
#include "Console.h"
#include "Export.h"
#include "PluginManager.h"
#include "modules/Items.h"
#include "modules/Units.h"
#include "modules/Buildings.h"
#include "DataDefs.h"
#include "df/world.h"
#include "df/item_threadst.h"
#include "df/item_clothst.h"
#include "df/building.h"
#include "df/building_stockpilest.h"


DFHACK_PLUGIN("combinethread")
REQUIRE_GLOBAL(world);

using namespace DFHack;
using namespace df::enums;

command_result df_combinethread (color_ostream &out, std::vector<std::string> & parameters);

DFhackCExport command_result plugin_init ( color_ostream &out, std::vector <PluginCommand> &commands)
{
    commands.push_back(PluginCommand(
        "combinethread",
        "Combines partially used spools of thread for re-use.",
        df_combinethread));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( color_ostream &out )
{
    return CR_OK;
}

static constexpr int32_t thread_full_dim = 15000;
static constexpr int32_t cloth_full_dim = 10000;

void combine_threads_in_vector(std::vector<df::item_threadst*>& vec, int& out_stat_filled, int& out_stat_emptied) {
    if (vec.size() < 2) return;
    auto itl = vec.begin();
    for (auto itr = itl + 1; itr != vec.end(); ++itr) {
        int32_t left_dim = (*itl)->dimension;
        int32_t right_dim = (*itr)->dimension;
        bool filled_up = right_dim + left_dim >= thread_full_dim;
        int32_t amount_to_combine = filled_up ? thread_full_dim - left_dim : right_dim;
        
        (*itl)->dimension = left_dim + amount_to_combine;
        (*itr)->dimension = right_dim - amount_to_combine;
        if (filled_up){
            ++itl;
            ++out_stat_filled;
        }
    }
    for( ; itl != vec.end(); ++itl) {
        if ((*itl)->dimension == 0) {
            ++out_stat_emptied;
            (*itl)->flags.bits.garbage_collect = true;
        }
    }
}

command_result df_combinethread (color_ostream& out, std::vector<std::string>& parameters)
{
    bool quiet = false;
    if (std::find(parameters.begin(), parameters.end(), "--quiet") != parameters.end()) quiet = true;
    CoreSuspender suspend;

    if (!quiet) out.print("COMBINING THREADS!\n");

    int filled_count=0;
    int emptied_count=0;

    // combine any partially-used threads of same material in containers in stockpiles
    for (std::size_t i = 0; i < world->buildings.other.STOCKPILE.size(); ++i) {
        df::building_stockpilest* sp = world->buildings.other.STOCKPILE[i];
        
        auto has_allowed = [](const std::vector<char>& vec) {
            return std::find_if(vec.begin(), vec.end(), [](const char&c){
                return static_cast<bool>(c);
            }) != vec.end();
        };

        if (!has_allowed(sp->settings.cloth.thread_plant) &&
            !has_allowed(sp->settings.cloth.thread_silk) && 
            !has_allowed(sp->settings.cloth.thread_yarn) &&
            !has_allowed(sp->settings.cloth.thread_metal))
        {
            continue;
        }
        std::map<std::tuple<int16_t, int32_t>, std::vector<df::item_threadst*>> threads_by_mat;

        auto insert_in_map = [&] (df::item_threadst* item, std::map<std::tuple<int16_t, int32_t>, std::vector<df::item_threadst*>>& my_map) {
            if (item->isDyed()) return;
            // skip full spools of thread
            if (item->dimension == thread_full_dim) return;
            
            // skip on following flags
            if (item->flags.bits.in_job |
                item->flags.bits.on_fire |
                item->flags.bits.melt |
                item->flags.bits.garbage_collect |
                item->flags.bits.dump |
                item->flags.bits.forbid |
                item->flags.bits.hostile |
                item->flags.bits.trader |
                item->flags.bits.artifact |
                item->flags.bits.artifact_mood |
                item->flags.bits.spider_web) return;

            auto key = std::make_tuple(item->getMaterial(), item->getMaterialIndex());
            auto map_it = my_map.find(key);
            if (map_it == my_map.end()) {
                map_it = my_map.emplace_hint(my_map.begin(), key, std::vector<df::item_threadst*>());
            }
            map_it->second.push_back(item);
            if (!quiet) out.print("Stockpile %d, Material(%d,%d): thread id=%d added to combine list\n",
                sp->id, item->getMaterial(), item->getMaterialIndex(), item->id);

        };

        std::vector<df::item*> sp_contents;
        Buildings::getStockpileContents(sp, &sp_contents);

        for (auto sp_item : sp_contents) {
            
            df::item_threadst* sp_thread = virtual_cast<df::item_threadst>(sp_item);

            if (sp_thread) {
                insert_in_map(sp_thread, threads_by_mat);
            }

            else {

                std::vector<df::item*> contained_items;
                Items::getContainedItems(sp_item, &contained_items);

                for (df::item* item : contained_items) {
                    df::item_threadst* item_thread = virtual_cast<df::item_threadst>(item);
                    if (item_thread) insert_in_map(item_thread, threads_by_mat);
                }

            }
        }

        for (auto& [key, vec] : threads_by_mat) {
            combine_threads_in_vector(vec, filled_count, emptied_count);
        }

    }

    if (filled_count || emptied_count) out.print("[combinethreads]: filled up %d spools of thread, emptied %d spools of thread\n", filled_count, emptied_count);

    return CR_OK;
}