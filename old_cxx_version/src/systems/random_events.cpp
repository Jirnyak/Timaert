#include "systems/random_events.h"

#include <cstdlib>

#include "core/game_context.h"
#include "systems/world_manager.h"
#include "systems/player.h"
#include "states/interaction_state.h"
#include "ecs/systems/spawn_system.h"
#include "core/tile_map.h"
#include "core/types.h"
#include "ecs/components/npc.h"
#include "ecs/world.h"
#include "entt/entt.hpp"
#include "systems/attributes.h"
#include "systems/economy.h"
#include "systems/skills.h"

Player* get_player(GameContext& ctx) {
    if (ctx.world_manager) {
        return &ctx.world_manager->player_ctrl.player();
    }
    return nullptr;
}

void trigger_fight(GameContext& ctx, NPCType type, const std::string& override_name) {
    if (!ctx.world_manager || !ctx.ecs_world)
        return;

    Player const* p = get_player(ctx);
    if (!p)
        return;

    // Спавним сущность ECS вместо старого NPC
    entt::entity const enemy = ecs::spawn_npc(*ctx.ecs_world, type, p->pos, -1, ctx.rng);

    if (ctx.ecs_world->registry.valid(enemy)) {
        // Настраиваем имя, если оно переопределено событием
        if (!override_name.empty()) {
            if (ctx.ecs_world->registry.all_of<ecs::CharacterInfo>(enemy)) {
                auto& info = ctx.ecs_world->registry.get<ecs::CharacterInfo>(enemy);
                info.set_name(override_name.c_str());
            }
        }

        // Враги из событий всегда агрессивны
        if (ctx.ecs_world->registry.all_of<ecs::CharacterInfo>(enemy)) {
            auto& info = ctx.ecs_world->registry.get<ecs::CharacterInfo>(enemy);
            info.set_personality("Aggressive");
        }

        // Устанавливаем цель для боевой системы
        ctx.battle_target_entity = enemy;
        // Запускаем взаимодействие (игрок может выбрать Talk/Fight/Trade)
        push_state(ctx, std::make_unique<InteractionState>());
    }
}

const std::vector<RandomEvent>& get_event_db() {
    static const std::vector<RandomEvent> DB = {
        // --- 0. Resources & Survival ---
        {
            "Old Shrine",
            "You find a crumbling altar covered in moss. It radiates a faint energy.",
            {
                {"Pray (Heal)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->combat_stats.current_hp = std::min(p->combat_stats.current_hp + 30, p->combat_stats.max_hp);
                }},
                {"Loot (Gold)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->inventory.add_capital(40);
                        p->reputation[(size_t)FactionID::Faction1] -= 2;
                    }
                }},
                {"Ignore", [](GameContext&) {}}
            }
        },
        {
            "Berry Bush",
            "A lush bush full of red berries. They look delicious.",
            {
                {"Eat", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->combat_stats.current_hp = std::min(p->combat_stats.current_hp + 10, p->combat_stats.max_hp);
                        // Небольшой шанс диареи/отравления не реализован, просто хил
                    }
                }},
                {"Harvest", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->inventory.add(ResourceType::Grain, 2); // Grain as abstract food
                }},
                {"Leave", [](GameContext&) {}}
            }
        },
        {
            "Abandoned Cart",
            "A broken cart lies on the side of the road. The owner is long gone.",
            {
                {"Search", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->inventory.add(ItemType::WoodLogs, 3);
                        p->inventory.add(ItemType::ClothRoll, 2);
                    }
                }},
                {"Walk away", [](GameContext&) {}}
            }
        },
        {
            "Heavy Rain",
            "A sudden downpour soaks you to the bone. It's cold and miserable.",
            {
                {"Endure (-Will)", [](GameContext& ctx) {
                }},
                {"Find Shelter (-Time)", [](GameContext& ctx) {
                     ctx.set_ticks(ctx.ticks() + 2000); // Ждем 2 часа (2000 ticks)
                }}
            }
        },
        {
            "Sunny Glade",
            "The sun shines brightly on this peaceful patch of grass.",
            {
                {"Rest (+Will)", [](GameContext& ctx) {
                }}
            }
        },
        
        // --- 5. Encounters & Trade ---
        {
            "Traveling Merchant",
            "A merchant rests by the road. 'Care to trade or share a meal?'",
            {
                {"Buy Rations (50g)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        if (p->inventory.get_capital() >= 50) {
                            p->inventory.remove_capital(50);
                            p->combat_stats.current_hp = std::min(p->combat_stats.current_hp + 50, p->combat_stats.max_hp);
                        }
                    }
                }},
                {"Rob him", [](GameContext& ctx) {
                     // Битва с торговцем
                     trigger_fight(ctx, NPCType::Merchant, "Angry Merchant");
                }},
                {"Leave", [](GameContext&) {}}
            }
        },
        {
            "Beggar",
            "A ragged man asks for a coin. 'Bless you, traveler.'",
            {
                {"Give 10g (+Rep)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        if (p->inventory.get_capital() >= 10) {
                            p->inventory.remove_capital(10);
                            p->reputation[(size_t)FactionID::Faction1] += 2;
                        }
                    }
                }},
                {"Kick him (-Rep)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Faction1] -= 5;
                    }
                }},
                {"Ignore", [](GameContext&) {}}
            }
        },
        {
            "Bounty Hunter",
            "A grim warrior looks at a wanted poster, then at you.",
            {
                {"Show ID", [](GameContext&) {}},
                {"Attack!", [](GameContext& ctx) {
                     trigger_fight(ctx, NPCType::Guard, "Bounty Hunter");
                }}
            }
        },
        {
            "Lost Child",
            "A crying child has lost their parents.",
            {
                {"Help (+Rep)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] += 10;
                }},
                {"Ignore (-Will)", [](GameContext& ctx) {
                }}
            }
        },
        {
            "Bard",
            "A bard offers to sing a song of your deeds.",
            {
                {"Listen (+Will)", [](GameContext& ctx) {
                }},
                {"Tip 20g (+Rep)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                         if (p->inventory.get_capital() >= 20) {
                             p->inventory.remove_capital(20);
                             p->reputation[(size_t)FactionID::Faction1] += 5;
                         }
                    }
                }}
            }
        },

        // --- 10. Combat & Danger ---
        {
            "Bandit Ambush",
            "You hear a twig snap. 'Your money or your life!'",
            {
                {"Fight!", [](GameContext& ctx) {
                    trigger_fight(ctx, NPCType::Bandit, "Highwayman");
                }},
                {"Pay 100g", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        if (p->inventory.get_capital() >= 100) p->inventory.remove_capital(100);
                        else trigger_fight(ctx, NPCType::Bandit, "Highwayman"); // Денег нет - драка
                    }
                }}
            }
        },
        {
            "Wolf Pack",
            "Growling shadows emerge from the bushes. Starving wolves.",
            {
                {"Defend yourself", [](GameContext& ctx) {
                     // Используем бандита как прокси для волка пока нет типа монстра
                     trigger_fight(ctx, NPCType::Bandit, "Alpha Wolf");
                }},
                {"Run (-Stamina)", [](GameContext& ctx) {
                }}
            }
        },
        {
            "Drunk Guard",
            "A city guard stumbles around, waving his sword.",
            {
                {"Avoid", [](GameContext&) {}},
                {"Steal Purse", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        if (random_u32_inclusive(ctx.rng, 100) > 50) {
                            p->inventory.add_capital(50);
                        } else {
                            trigger_fight(ctx, NPCType::Guard, "Drunk Guard");
                        }
                    }
                }}
            }
        },
        {
            "Trap!",
            "You step into a snare trap!",
            {
                {"Break free (-HP)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->combat_stats.current_hp -= 15;
                }},
                {"Wait for help", [](GameContext& ctx) {
                     trigger_fight(ctx, NPCType::Bandit, "Trapper");
                }}
            }
        },
        {
            "Duel Challenge",
            "A wandering knight challenges you to a duel for honor.",
            {
                {"Accept", [](GameContext& ctx) {
                    trigger_fight(ctx, NPCType::Guard, "Knight Errant");
                }},
                {"Decline (-Rep)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] -= 5;
                }}
            }
        },

        // --- 15. Lore & Atmosphere ---
        {
            "Ancient Ruins",
            "Stones marked with forgotten runes jut from the earth.",
            {
                {"Examine", [](GameContext& ctx) {
                }},
                {"Touch", [](GameContext& ctx) {
                     if (auto* p = get_player(ctx)) {
                         p->combat_stats.current_hp -= 10; // Shock
                     }
                }}
            }
        },
        {
            "Hanged Man",
            "A grim reminder of the law hangs from a tree.",
            {
                {"Look away", [](GameContext&) {}},
                {"Search pockets", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->inventory.add_capital(5);
                    }
                }}
            }
        },
        {
            "Fairy Circle",
            "Mushrooms growing in a perfect circle.",
            {
                {"Step inside", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->visual_x += 1000.0f; // Glitch visual effect ;)
                    }
                }},
                {"Destroy", [](GameContext& ctx) {
                }}
            }
        },
        {
            "Shooting Star",
            "A bright streak across the night sky.",
            {
                {"Make a wish (+Will)", [](GameContext& ctx) {
                }}
            }
        },
        {
            "Battlefield",
            "Old rusted armor and bones scatter the field.",
            {
                {"Scavenge", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->inventory.add(ItemType::IronOre, 5);
                }}
            }
        },

        // --- 20. 18+ Soft / Tease / Lust Mechanics ---
        {
            "Hot Spring",
            "You find a natural hot spring. Steam rises invitingly.",
            {
                {"Bath (Heal)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->combat_stats.current_hp = p->combat_stats.max_hp;
                    }
                }},
                {"Play with self (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                    }
                }}
            }
        },
        {
            "Peeping Tom",
            "You see someone bathing in the river nearby.",
            {
                {"Watch (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                    }
                }},
                {"Approach", [](GameContext& ctx) {
                    trigger_fight(ctx, NPCType::Peasant, "Startled Bather");
                }},
                {"Leave", [](GameContext&) {}}
            }
        },
        {
            "Torn Clothes",
            "A branch snags your clothes, ripping them in a precarious spot.",
            {
                {"Cover up", [](GameContext& ctx) {
                }},
                {"Embrace it (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },
        {
            "Aphrodisiac Pollen",
            "You walk through a field of strange pink flowers. The scent is heady.",
            {
                {"Hold breath", [](GameContext& ctx) {
                }},
                {"Inhale (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                    }
                }}
            }
        },
        {
            "Strange Potion",
            "You find a bottle with pink liquid labeled 'Drink Me'.",
            {
                {"Drink", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->combat_stats.current_hp += 20;
                    }
                }},
                {"Throw away", [](GameContext&) {}}
            }
        },
        {
            "The Lusty Maid",
            "A tavern maid winks at you and bends over further than necessary.",
            {
                {"Flirt back", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }},
                {"Ignore", [](GameContext&) {}}
            }
        },
        {
            "Tentacle Plant",
            "A strange plant wraps around your leg. It feels... slimy.",
            {
                {"Cut it loose", [](GameContext& ctx) {
                     // Combat check? Just damage for now
                     if (auto* p = get_player(ctx)) p->combat_stats.current_hp -= 5;
                }},
                {"Let it linger (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                    }
                }}
            }
        },
        {
            "Broken Armor",
            "Your gear feels loose. A strap has broken, exposing your skin.",
            {
                {"Fix it", [](GameContext&) {}},
                {"Leave it (+Exhibitionism)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },
        {
            "Dirty Magazine",
            "Someone lost a scroll with lewd drawings.",
            {
                {"Read (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                    }
                }},
                {"Burn it", [](GameContext& ctx) {
                }}
            }
        },
        {
            "Succubus Whisper",
            "A voice in your head suggests naughty things.",
            {
                {"Resist (-Will)", [](GameContext& ctx) {
                }},
                {"Give in (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },

        // --- 30. Harder 18+ / Fetish / Non-Con implied ---
        {
            "Bandit Inspection",
            "Bandits surround you. 'Strip and we might let you go.'",
            {
                {"Fight!", [](GameContext& ctx) {
                    trigger_fight(ctx, NPCType::Bandit, "Perverted Bandit");
                }},
                {"Strip (-Will, +Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                        p->reputation[(size_t)FactionID::Faction2] += 5; // They like you now
                    }
                }}
            }
        },
        {
            "Slime Trap",
            "You step into a puddle of sticky, vibrating slime.",
            {
                {"Struggle", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->combat_stats.current_hp -= 10;
                    }
                }},
                {"Enjoy (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                    }
                }}
            }
        },
        {
            "Goblin Ambush",
            "Small green hands grab at your clothes!",
            {
                {"Kick them", [](GameContext& ctx) {
                    trigger_fight(ctx, NPCType::Bandit, "Goblin Groper");
                }},
                {"Submit", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->inventory.set_capital(0); // Stole money
                    }
                }}
            }
        },
        {
            "Hypnotic Dancer",
            "A dancer moves in a way that captures your mind.",
            {
                {"Look away", [](GameContext& ctx) {
                }},
                {"Stare (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },
        {
            "Magic Mirror",
            "A mirror shows you performing lewd acts.",
            {
                {"Smash it", [](GameContext&) {}},
                {"Watch (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },

        // --- 35. Factions & World ---
        {
            "Tax Collector",
            "Kingdom officials demand a toll for the road.",
            {
                {"Pay 50g", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        if(p->inventory.get_capital() >= 50) {
                            p->inventory.remove_capital(50);
                            p->reputation[(size_t)FactionID::Faction1] += 2;
                        } else {
                            p->reputation[(size_t)FactionID::Faction1] -= 5;
                        }
                    }
                }},
                {"Refuse", [](GameContext& ctx) {
                    trigger_fight(ctx, NPCType::Guard, "Tax Collector");
                }}
            }
        },
        {
            "Rebel Recruiter",
            "A hooded figure asks if you hate the King.",
            {
                {"Yes (+Outlaws)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Faction2] += 10;
                        p->reputation[(size_t)FactionID::Faction1] -= 10;
                    }
                }},
                {"No (Report him)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Faction2] -= 10;
                        p->reputation[(size_t)FactionID::Faction1] += 5;
                        trigger_fight(ctx, NPCType::Bandit, "Rebel Spy");
                    }
                }}
            }
        },
        {
            "Smugglers",
            "Men moving crates at night.",
            {
                {"Join them", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->inventory.add(ResourceType::Spices, 1);
                        p->reputation[(size_t)FactionID::Faction1] -= 5;
                    }
                }},
                {"Attack", [](GameContext& ctx) {
                    trigger_fight(ctx, NPCType::Bandit, "Smuggler");
                }}
            }
        },
        
        // --- 38. Random Fillers (Getting to 80) ---
        {"Windy Day", "A strong wind blows.", {{"Wait", [](GameContext&){}}}},
        {"Fog", "You can't see much.", {{"Careful", [](GameContext&){}}}},
        {"Distant Thunder", "A storm is coming.", {{"Hurry", [](GameContext&){}}}},
        {"Stumble", "You trip over a root.", {{"Ouch (-HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life-=2; }}}},
        {"Wild Flowers", "Pretty flowers.", {{"Pick", [](GameContext&){}}}},
        {"Strange smell", "Something smells rotten.", {{"Ignore", [](GameContext&){}}}},
        {"Itch", "Something bit you.", {{"Scratch", [](GameContext&){}}}},
        {"Deja vu", "Have you been here before?", {{"Confused", [](GameContext&){}}}}, // 48
        
        {"Found Arrow", "A decent arrow.", {{"Take", [](GameContext&){}}}},
        {"Lost Shoe", "Someone lost a shoe.", {{"Weird", [](GameContext&){}}}},
        {"Crow", "It stares at you.", {{"Shoo", [](GameContext&){}}}},
        {"Silence", "It is too quiet.", {{"Alert", [](GameContext&){}}}},
        {"Full Moon", "The moon is beautiful.", {{"Howl", [](GameContext&){}}}},
        {"Sneeze", "Dust in the air.", {{"Bless you", [](GameContext&){}}}},
        {"Hunger", "Tummy rumbles.", {{"Eat ration", [](GameContext&){}}}},
        {"Thirst", "Throat dry.", {{"Drink", [](GameContext&){}}}},
        {"Tired", "Need sleep.", {{"Yawn", [](GameContext&){}}}},
        {"Boredom", "Nothing happens.", {{"Hum a tune", [](GameContext&){}}}}, // 58
        
        // 18+ Fillers
        {"Wardrobe Malfunction", "Button pops.", {{"Fix", [](GameContext&){}}}},
        {"Sticky Sap", "Sticky stuff on a tree.", {{"Touch", [](GameContext&){}}}}, // 68

        // Specific Skills Learning
        {"Old Master", "A monk offers to teach you to focus.", {{"Learn (+Focus)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }}}},
        {"Street Fighter", "Teaches you a dirty trick.", {{"Learn (+Dirty Blow)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }}}},
        {"Courtesan", "She teaches you to wink.", {{"Learn (+Wink)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }}}},
        
        // Items
        {"Rusty Sword", "Lying in the grass.", {{"Take", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 1); }}}},
        {"Bag of Salt", "Spilled but usable.", {{"Scrape up", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Salt, 2); }}}},
        {"Silk Scarf", "Smells of perfume.", {{"Keep", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Cloth, 1); }}}},
        {"Gold Nugget", "Lucky!", {{"Rich!", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add_capital(100); }}}},
        {"Fake Coin", "It's wood painted gold.", {{"Damn", [](GameContext&){}}}},
        {"Map Fragment", "Shows nothing useful.", {{"Toss", [](GameContext&){}}}}, // 78
        
        {"The End of Road", "The path simply stops here.", {{"Turn back", [](GameContext&){}}}},
                {"Game Bug", "You see the code matrix.", {{"Ignore", [](GameContext&){}}}},

        // --- NEW EVENTS START HERE ---

        // --- CITY & SOCIAL LIFE ---
        {
            "Red Light District",
            "Women of the night line the street. The air smells of cheap perfume.",
            {
                {"Buy Service (50g)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        if(p->inventory.get_capital() >= 50) {
                            p->inventory.remove_capital(50);
                        }
                    }
                }},
                {"Sell Body (+Gold, -Will)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->inventory.add_capital(40);
                        p->reputation[(size_t)FactionID::Faction1] -= 5;
                    }
                }},
                {"Preach Morality", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Bandit, "Angry Pimp");
                }}
            }
        },
        {
            "Local Festival",
            "The town is celebrating the harvest. Everyone is dancing.",
            {
                {"Dance (+Will)", [](GameContext& ctx){
                }},
                {"Steal Food", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->inventory.add(ResourceType::Grain, 5);
                        p->reputation[(size_t)FactionID::Faction1] -= 2;
                    }
                }},
                {"Get Drunk (Wine)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->inventory.add(ResourceType::Wine, 1);
                    }
                }}
            }
        },
        {
            "Public Execution",
            "A criminal is being hanged in the square.",
            {
                {"Watch (-Will)", [](GameContext& ctx){
                }},
                {"Save him!", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Faction1] -= 50;
                        p->reputation[(size_t)FactionID::Faction2] += 50;
                        trigger_fight(ctx, NPCType::Guard, "Executioner");
                    }
                }}
            }
        },
        {
            "Dark Alley",
            "A whisper from the shadows: 'Want some fun?'",
            {
                {"Approach", [](GameContext& ctx){
                     trigger_fight(ctx, NPCType::Bandit, "Mugger");
                }},
                {"Ignore", [](GameContext&){}}
            }
        },
        {
            "Bathhouse",
            "A public bathhouse. Mixed gender today.",
            {
                {"Enter (10g)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        if(p->inventory.get_capital() >= 10) {
                            p->inventory.remove_capital(10);
                            p->combat_stats.current_hp = p->combat_stats.max_hp;
                        }
                    }
                }},
                {"Peep (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },

        // --- WILDERNESS & EXPLORATION ---
        {
            "Giant Mushroom",
            "It pulses with a strange neon light.",
            {
                {"Eat it", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->combat_stats.max_hp += 5;
                        p->combat_stats.current_hp -= 10;
                    }
                }},
                {"Harvest", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Spices, 2);
                }}
            }
        },
        {
            "Hunter's Trap",
            "A bear trap hidden in leaves.",
            {
                {"Disarm (+Iron)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 1);
                }},
                {"Step in it (-HP)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->combat_stats.current_hp -= 25;
                }}
            }
        },
        {
            "Hermit's Shack",
            "An old man lives here. He looks crazy.",
            {
                {"Talk", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }},
                {"Rob", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->inventory.add_capital(5);
                }}
            }
        },
        {
            "Cursed Idol",
            "An ugly statue that makes you feel uneasy.",
            {
                {"Destroy (+Will)", [](GameContext& ctx){
                }},
                {"Worship (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },
        {
            "Hot Day",
            "The sun is unbearable.",
            {
                {"Strip slightly (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                    }
                }},
                {"Suffer (-Will)", [](GameContext& ctx){
                }}
            }
        },

        // --- EROTIC & FETISH (18+) ---
        {
            "Slime Puddle",
            "It looks like normal water, but it's thick and moves.",
            {
                {"Step in", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        // Simulating slime attack
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }},
                {"Avoid", [](GameContext&){}}
            }
        },
        {
            "Exhibitionist",
            "A stranger opens their coat as you pass.",
            {
                {"Stare (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }},
                {"Laugh (+Will)", [](GameContext& ctx){
                }}
            }
        },
        {
            "Tentacle Vine",
            "A plant grabs your leg and creeps up.",
            {
                {"Slash it", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Wood, 1);
                }},
                {"Enjoy (+Lust, -Will)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },
        {
            "Hypnotic Pendulum",
            "You find a shiny amulet swinging on a branch.",
            {
                {"Watch it (-Will)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }},
                {"Take it (+Gold)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->inventory.add_capital(30);
                }}
            }
        },
        {
            "Aphrodisiac Gas",
            "A pink cloud erupts from the ground!",
            {
                {"Breathe (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                    }
                }},
                {"Run", [](GameContext&){}}
            }
        },
        {
            "Peeping Hole",
            "A hole in the wall of a changing room.",
            {
                {"Peep (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                    }
                }},
                {"Ignore", [](GameContext&){}}
            }
        },
        {
            "Magic Corset",
            "You find a corset. It looks magical.",
            {
                {"Wear it (+Def, +Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->combat_stats.max_hp += 20;
                    }
                }},
                {"Sell (20g)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->inventory.add_capital(20);
                }}
            }
        },
        {
            "Nude Beach",
            "Locals are swimming naked here.",
            {
                {"Join them (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }},
                {"Watch from bushes", [](GameContext& ctx){
                }}
            }
        },
        {
            "Oil Bottle",
            "Slippery massage oil.",
            {
                {"Use on self (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }},
                {"Drink? (Sick)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->combat_stats.current_hp -= 5;
                }}
            }
        },
        {
            "Bound Person",
            "Someone is tied up. They look... aroused?",
            {
                {"Untie", [](GameContext& ctx){
                     if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] += 2;
                }},
                {"Tease (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Faction2] += 2;
                    }
                }}
            }
        },

        // --- COMBAT & DANGER ---
        {
            "Stalking Beast",
            "Yellow eyes watch you from the dark.",
            {
                {"Attack", [](GameContext& ctx){
                     trigger_fight(ctx, NPCType::Bandit, "Stalker");
                }},
                {"Flee", [](GameContext& ctx){
                }}
            }
        },
        {
            "Rival Adventurer",
            "He wants your loot.",
            {
                {"Duel", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Guard, "Rival");
                }},
                {"Share (20g)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        if(p->inventory.get_capital() >= 20) p->inventory.remove_capital(20);
                        else trigger_fight(ctx, NPCType::Guard, "Angry Rival");
                    }
                }}
            }
        },
        {
            "Drunk Giant",
            "A massive man blocks the path.",
            {
                {"Sneak past", [](GameContext&){}},
                {"Fight", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Woodcutter, "Giant");
                }}
            }
        },
        {
            "Witch's Hut",
            "A creepy hut on chicken legs.",
            {
                {"Enter", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Witch, "Baba Yaga");
                }},
                {"Burn it", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Wilderness] -= 10;
                    }
                }}
            }
        },
        {
            "Slavers",
            "They want to capture you alive.",
            {
                {"Fight!", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Bandit, "Slaver");
                }},
                {"Surrender (Bad idea)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->inventory.set_capital(0);
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },

        // --- SKILL LEARNING & UPGRADES ---
        {
            "Training Dummy",
            "An old straw dummy.",
            {
                {"Practice (+Exp)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },
        {
            "Yoga Instructor",
            "She is very flexible.",
            {
                {"Learn (+Flexibility)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }}
            }
        },
        {
            "Old Pervert",
            "He offers to teach you a 'special' technique.",
            {
                {"Learn (+Grope)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                        p->reputation[(size_t)FactionID::Faction1] -= 1;
                    }
                }},
                {"Refuse", [](GameContext&){}}
            }
        },
        {
            "Library",
            "So many books.",
            {
                {"Study Magic (+Fireball)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Bodybuilding);
                    }
                }},
                {"Read Erotica (+Lust)", [](GameContext& ctx){
                }}
            }
        },
        {
            "Gym",
            "Weights made of stone.",
            {
                {"Lift (+HP)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->combat_stats.max_hp += 2;
                    }
                }}
            }
        },

        // --- MISC / FLUFF (Quick Events) ---
        {"Bad Smell", "Eww.", {{"Cover nose", [](GameContext&){}}}},
        {"Butterfly", "It lands on your nose.", {{"Sneeze", [](GameContext&){}}}},
        {"Loose Rock", "You almost tripped.", {{"Curse", [](GameContext&){}}}},
        {"Coin on ground", "A copper piece.", {{"Take (1g)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add_capital(1); }}}},
        {"Rain", "Again?", {{"Sigh", [](GameContext&){}}}},
        {"Rainbow", "Somewhere over it...", {{"Smile", [](GameContext&){}}}},
        
        {"Broken Wheel", "Someone abandoned a cart part.", {{"Take Wood", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Wood, 1); }}}},
        {"Wild Apple", "A sour apple.", {{"Eat", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life+=2; }}}},
        {"Ant Hill", "Don't sit there.", {{"Poke", [](GameContext&){}}}},
        {"Spider", "A tiny one.", {{"Squish", [](GameContext&){}}}},
        {"Snail", "Slowly moving.", {{"Watch", [](GameContext&){}}}},
        {"Echo", "Hello!", {{"Shout", [](GameContext&){}}}},
        {"Puddle", "Splash.", {{"Jump", [](GameContext&){}}}},
        {"Flower", "Smells nice.", {{"Sniff", [](GameContext&){}}}},
        {"Bee", "Buzz off.", {{"Run", [](GameContext&){}}}},

        {"Hiccups", "You can't stop.", {{"Hold breath", [](GameContext&){}}}},
        {"Itchy Back", "Can't reach it.", {{"Rub on tree", [](GameContext&){}}}},
        {"Loose Button", "Pop.", {{"Fix", [](GameContext&){}}}},
        {"Hole in pocket", "Lost a coin.", {{"Damn (-1g)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.remove_capital(1); }}}},
        {"Scary Shadow", "Just a tree.", {{"Phew", [](GameContext&){}}}},
        {"Owl", "Hoot hoot.", {{"Listen", [](GameContext&){}}}},
        {"Firefly", "Glows in dark.", {{"Catch", [](GameContext&){}}}},
        {"Shoe Stone", "Annoying.", {{"Remove", [](GameContext&){}}}},
        {"Yawn", "Contagious.", {{"Sleepy", [](GameContext&){}}}},

        // --- MORE FETISH / LUST ---
        {"Cream Pie", "A bakery selling pies.", {{"Eat", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life+=5; }}}},

        {"Rope", "Useful for binding.", {{"Keep (+Rope)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Cloth, 1); }}}},
        {"Whip", "For horses, right?", {{"Keep", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }}}},
        {"Condom", "Primitive protection.", {{"Take", [](GameContext&){}}}},

        // --- EXPANSION PACK: ADVENTURE & 18+ ---

        // 1. Wild Encounters
        {"Wild Horse", "A magnificent stallion grazes nearby.", {
            {"Ignore", [](GameContext&){}}
        }},
        {"Bear Trap", "Hidden in the leaves.", {
            {"Disarm (+Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 1); }},
            {"Step in (-HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->combat_stats.current_hp -= 20; }}
        }},
        {"Giant Spider", "It drops from a tree!", {
            {"Fight", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Giant Spider"); }},
        }},
        {"Injured Wolf", "It whines in pain.", {
            {"Kill (+Food)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Grain, 1); }}
        }},
        {"Bee Hive", "Full of honey.", {
            {"Take Honey (+Food, -HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->inventory.add(ResourceType::Grain, 2); p->combat_stats.current_hp -= 5; } }},
            {"Leave", [](GameContext&){}}
        }},

        // 2. Mystical & Magic
        {"Glowing Rune", "Carved into a rock. It hums.", {
            {"Study (+Int)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }}
        }},
        {"Magic Mirror", "Reflects your deepest desires.", {
        }},
        {"Cursed Sword", "A black blade stuck in the ground.", {
            {"Take (-HP, +Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->combat_stats.current_hp -= 15; p->inventory.add(ResourceType::Iron, 3); } }},
            {"Leave", [](GameContext&){}}
        }},
        {"Fountain of Youth", "The water sparkles.", {
            {"Drink (Heal)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->combat_stats.current_hp = p->combat_stats.max_hp; }},
            {"Bottle it", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Wine, 1); }}
        }},
        {"Ghost", "A transparent figure points somewhere.", {
            {"Exorcise", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }}
        }},

        // 3. Social & City
        {"Card Game", "Locals invite you to play.", {
            {"Play (-10g, 50% chance +30g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if(p->inventory.get_capital() >= 10) {
                        p->inventory.remove_capital(10);
                        if(rand()%2 == 0) p->inventory.add_capital(30);
                    }
                }
            }},
            {"Cheating (+Skill)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }}
        }},
        {"Pickpocket", "Someone bumps into you.", {
            {"Check Pockets", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { if(rand()%2==0) p->inventory.remove_capital(5); } }},
            {"Catch him!", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Thief"); }}
        }},
        {"Drunkard", "He offers you a drink.", {
            {"Decline", [](GameContext&){}}
        }},
        {"Preacher", "He shouts about doom.", {
            {"Argue", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] -= 1; }}
        }},
        {"Lost Dog", "Looks hungry.", {
            {"Shoo", [](GameContext&){}}
        }},

        // 4. Erotic / 18+ Scenarios
        {"Hot Spring (Mixed)", "Men and women bathing together.", {
        }},
        {"Succubus Trap", "A beautiful woman calls for help.", {
            {"Help", [](GameContext& ctx){ 
                trigger_fight(ctx, NPCType::Witch, "Succubus");
            }},
            {"Ignore", [](GameContext&){}}
        }},
        {"Torn Dress", "A woman's dress is caught on a bush.", {
        }},
        {"Aphrodisiac Merchant", "Sells special potions.", {
            {"Buy Potion (50g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if(p->inventory.get_capital() >= 50) {
                        p->inventory.remove_capital(50);
                    }
                }
            }},
        }},
        {"Magic Bindings", "You step into a magical snare.", {
        }},
        {"Nude Statue", "Carved with incredible detail.", {
        }},
        {"Peeping Goblin", "Watching you pee.", {
            {"Kick him", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Peeping Goblin"); }},
        }},
        {"Wet Clothes", "Rain makes your clothes see-through.", {
        }},
        {"Massage Parlor", " signs promise 'Happy Endings'.", {
            {"Enter (100g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if(p->inventory.get_capital() >= 100) {
                        p->inventory.remove_capital(100);
                    }
                }
            }},
            {"Work there (+Gold)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    p->inventory.add_capital(50);
                    p->reputation[(size_t)FactionID::Faction1] -= 5;
                }
            }}
        }},
        {"Tentacle Monster", "Slimy appendages emerge!", {
            {"Fight", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Tentacle Beast"); }},
            {"Submit (Game Over?)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) { 
                    p->combat_stats.current_hp -= 10;
                } 
            }}
        }},

        // 5. Encounters & Loot
        {"Abandoned Camp", "Embers still warm.", {
            {"Search", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Wood, 2); }},
            {"Rest", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->combat_stats.current_hp += 10; }}
        }},
        {"Broken Cart", "Merchandise scattered.", {
            {"Loot (+Cloth)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Cloth, 3); }},
        }},
        {"Dead Adventurer", "Clutched a map.", {
            {"Take Map (Nothing)", [](GameContext&){}},
            {"Loot Gear (+Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 1); }}
        }},
        {"Wild Berries", "Red and juicy.", {
            {"Eat (+HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->combat_stats.current_hp += 5; }},
            {"Collect", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Grain, 1); }}
        }},
        {"Ancient Obelisk", "Covered in moss.", {
            {"Ignore", [](GameContext&){}}
        }},

        // 6. Risky Business
        {"Slave Auction", "Humans for sale.", {
            {"Buy Slave (200g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if(p->inventory.get_capital() >= 200) {
                        p->inventory.remove_capital(200);
                        p->reputation[(size_t)FactionID::Faction2] += 10;
                        // Mechanics for owning slave not implemented, just stat change
                    }
                }
            }},
            {"Attack Slavers", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Slaver Boss"); }}
        }},
        {"Strange Potion", "Label says 'Growth'.", {
            {"Drink (+Str?)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->combat_stats.max_hp += 10; p->combat_stats.current_hp -= 10; } }},
            {"Sell (50g)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add_capital(50); }}
        }},
        {"Gambling Den", "Smoke and dice.", {
            {"Bet High (100g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if (p->inventory.get_capital() >= 100) {
                        p->inventory.remove_capital(100);
                        if(rand()%3 == 0) p->inventory.add_capital(300);
                    }
                }
            }},
            {"Leave", [](GameContext&){}}
        }},
        {"Witch's Brew", "Smells like lust.", {
            {"Spill", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Witch, "Angry Witch"); }}
        }},
        {"Magic Mushroom", "Colors are melting.", {
            {"Ignore", [](GameContext&){}}
        }},

        // 7. Random Fluff (Quick)
        {"Itch", "Mosquito bite.", {{"Scratch", [](GameContext&){}}}},
        {"Sneeze", "Dust.", {{"Bless you", [](GameContext&){}}}},
        {"Stumble", "Tripped on a root.", {{"Ouch (-1 HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->combat_stats.current_hp -= 1; }}}},
        {"Lost Coin", "Found 1 gold.", {{"Take", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add_capital(1); }}}},
        {"Ugly Bug", "Eww.", {{"Squish", [](GameContext&){}}}},
        {"Distant Howl", "Scary.", {{"Hurry", [](GameContext&){}}}},
        {"Old Boot", "Trash.", {{"Ignore", [](GameContext&){}}}},
        {"Rainbow", "Pretty.", {{"Watch", [](GameContext&){}}}},
        {"Deja Vu", "Have I been here?", {{"Confused", [](GameContext&){}}}},
        {"Hunger", "Tummy rumbles.", {{"Eat ration", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.remove(ResourceType::Grain, 1); }}}},
        {"Thirst", "Mouth dry.", {{"Drink", [](GameContext&){}}}},
        {"Tired", "Need sleep.", {{"Yawn", [](GameContext&){}}}},
        {"Bored", "Nothing happens.", {{"Hum", [](GameContext&){}}}},
        {"Silence", "Too quiet.", {{"Alert", [](GameContext&){}}}},

        // 8. Skill Trainers
        {"Old Monk", "Teaches discipline.", {
            {"Learn (+Meditate)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }},
            {"Rob", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Peasant, "Monk"); }}
        }},
        {"Retired General", "Teaches war.", {
            {"Learn (+WarCry)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }},
            {"Duel", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Guard, "General"); }}
        }},
        {"Seductress", "Teaches love.", {
        }},
        {"Thief Master", "Teaches stealth.", {
            {"Learn (+Backstab)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->learn_skill(SkillID::Bodybuilding); p->reputation[(size_t)FactionID::Faction1] -= 5; } }}
        }},
        {"Mad Mage", "Mumbles spells.", {
            {"Learn (+Spark)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }},
            {"Run", [](GameContext&){}}
        }},

        // 9. Items
        {"Lost Backpack", "Full of supplies.", {
            {"Loot", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->inventory.add(ResourceType::Grain, 2); p->inventory.add(ResourceType::Cloth, 2); } }}
        }},
        {"Weapon Cache", "Bandit stash.", {
            {"Take Sword (+Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 2); }},
            {"Leave trap", [](GameContext&){}}
        }},
        {"Silk Lingerie", "Fine quality.", {
            {"Keep (+Cloth)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Cloth, 1); }},
        }},
        {"Jewelry Box", "Locked.", {
            {"Break (+Gold)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add_capital(40); }},
            {"Pick lock", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }} // Learning by doing
        }},
        {"Strange Idol", "Vibrates.", {
            {"Sell (10g)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add_capital(10); }}
        }},

        // 10. Final Batch
        {"Landslide", "Rocks falling.", {{"Dodge", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->combat_stats.current_hp -= 5; }}}},
        {"Earthquake", "Ground shakes.", {{"Hold on", [](GameContext&){}}}},
        {"Meteor", "Crashes nearby.", {{"Investigate (+Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 5); }}}},

        // --- LORE-BASED EVENTS FROM CHARACTERS.MD ---

        // 1. Holy Empire Events
        {
            "Pilgrims of Light",
            "Followers of the Path of Light march through the land, preaching salvation.",
            {
                {"Listen (+Will)", [](GameContext& ctx){
                }},
                {"Criticize (-Rep)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] -= 5;
                }},
                {"Walk away", [](GameContext&){}}
            }
        },
        {
            "Paladins Hunting",
            "Holy warriors pass through, searching for mages. Their swords shine with divine light.",
            {
                {"Hide your magic", [](GameContext& ctx){
                }},
                {"Greet them as ally", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] += 10;
                }},
                {"Flee into forest", [](GameContext&){}}
            }
        },
        {
            "Magebouncers Patrol",
            "Elite warriors clad in anti-magic armor march past. They look terrifying.",
            {
                {"Pray they pass you by", [](GameContext&){}},
                {"Test your magic secretly", [](GameContext& ctx){
                }}
            }
        },
        {
            "Sacred Temple",
            "A grand temple of Light stands before you. Inside, bells ring.",
            {
                {"Enter and pray (+Will)", [](GameContext& ctx){
                }},
                {"Avoid (prefer magic)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] -= 2;
                }},
                {"Burn it", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Guard, "Temple Knight");
                }}
            }
        },
        {
            "Visir's Tax Demand",
            "An official from the Empire demands tribute.",
            {
                {"Pay (50g, +Rep)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        if(p->inventory.get_capital() >= 50) {
                            p->inventory.remove_capital(50);
                            p->reputation[(size_t)FactionID::Faction1] += 5;
                        }
                    }
                }},
                {"Refuse (-Rep, Fight)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] -= 10;
                    trigger_fight(ctx, NPCType::Guard, "Tax Visir");
                }}
            }
        },

        // 2. Magika Kingdom Events
        {
            "Mage Tower Ruin",
            "Ruins of a once-great academy crumble. Arcane energy still pulses weakly.",
            {
                {"Study the ruins (+Skill)", [](GameContext& ctx){
                }},
                {"Loot for artifacts", [](GameContext& ctx){
                }}
            }
        },
        {
            "Archmagus Gathering",
            "Powerful mages meet in secret, discussing the fall of their civilization.",
            {
                {"Eavesdrop (+Will, -Danger)", [](GameContext& ctx){
                }},
                {"Interrupt them", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Peasant, "Archmagus");
                }},
                {"Flee quietly", [](GameContext&){}}
            }
        },
        {
            "Czar-Peasant's Monument",
            "A statue of the legendary Czar stands here, wielding his black spear. Liberated peasants leave flowers.",
            {
                {"Pray to it", [](GameContext& ctx){
                }},
                {"Destroy it (-Rep with Free)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->reputation[(size_t)FactionID::Faction1] += 10; p->reputation[(size_t)FactionID::Faction2] -= 20; }
                    trigger_fight(ctx, NPCType::Peasant, "Peasant Guardian");
                }},
                {"Study the craftsmanship", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding);
                }}
            }
        },
        {
            "Old Magic School",
            "A small academy where children learn Basic Magika. They look hopeful but afraid.",
            {
                {"Teach them (+Rep)", [](GameContext& ctx){
                }},
                {"Rob them", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->inventory.add_capital(30); p->reputation[(size_t)FactionID::Faction2] -= 30; }
                }}
            }
        },
        {
            "Forbidden Black Artifact",
            "A pulsing black object floats in a sealed chamber. Your blood sings near it.",
            {
                {"Touch it (+Lust, -Will)", [](GameContext& ctx){
                }},
                {"Destroy it (+Will)", [](GameContext& ctx){
                }},
                {"Leave it alone", [](GameContext&){}}
            }
        },

        // 3. Cult & Dark Events
        {
            "Secret Cult Meeting",
            "Robed figures whisper about the prophecy and the 'black child' to come.",
            {
                {"Join them (-Rep, +Dark Power)", [](GameContext& ctx){
                }},
                {"Attack them", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Bandit, "Cult Leader");
                }},
                {"Spy from shadows", [](GameContext& ctx){
                }}
            }
        },
        {
            "Dark Shrine",
            "An altar dedicated to something ancient and hungry. Bones litter the ground.",
            {
                {"Worship (+Lust, -Will)", [](GameContext& ctx){
                }},
                {"Cleanse it (Holy water)", [](GameContext& ctx){
                }},
                {"Ignore evil", [](GameContext&){}}
            }
        },
        {
            "Whispers of the Dead",
            "Voices in your mind: 'The shadows return... join us... power eternal...'",
            {
                {"Resist (-Will)", [](GameContext& ctx){
                }},
                {"Listen (+Lust, +Insight)", [](GameContext& ctx){
                }},
                {"Pray to Light", [](GameContext& ctx){
                }}
            }
        },

        // 4. Prophecy & Destiny Events
        {
            "Oracle's Vision",
            "A blind seer grabs you. 'I see... I see the black child coming. You are near it.'",
            {
                {"Demand answers", [](GameContext& ctx){
                }},
                {"Pay for knowledge (50g)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        if(p->inventory.get_capital() >= 50) {
                            p->inventory.remove_capital(50);
                            p->learn_skill(SkillID::Bodybuilding);
                        }
                    }
                }},
                {"Flee the prophecy", [](GameContext& ctx){
                }}
            }
        },
        {
            "Time Runs Short",
            "The sun seems dimmer. Magic in the air feels... fading. An age is ending.",
            {
                {"Accept the change", [](GameContext& ctx){
                }},
                {"Fight the darkness", [](GameContext& ctx){
                }}
            }
        },
        {
            "Omen of Change",
            "The stars have moved. Scholars and mages gather in alarm. The prophecy stirs.",
            {
                {"Investigate (+Knowledge)", [](GameContext& ctx){
                }},
                {"Ignore it", [](GameContext&){}}
            }
        },

        // 5. Merchant & Trade Events
        {
            "Tymert Trader",
            "A merchant from the free city of Tymert. He deals in forbidden goods to all sides.",
            {
                {"Buy rare items (50g)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        if(p->inventory.get_capital() >= 50) {
                            p->inventory.remove_capital(50);
                            p->inventory.add(ResourceType::Spices, 5);
                        }
                    }
                }},
                {"Hire him as guide", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->move_progress += 100; p->inventory.add_capital(10); }
                }},
                {"Ask about the prophecy", [](GameContext& ctx){
                }}
            }
        },
        {
            "Black Market",
            "Whispered deals in dark alleys. Artifacts, potions, forbidden knowledge.",
            {
                {"Buy (+Lust potion)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        if(p->inventory.get_capital() >= 40) {
                            p->inventory.remove_capital(40);
                        }
                    }
                }},
                {"Become a dealer (+Gold)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->inventory.add_capital(100); p->reputation[(size_t)FactionID::Faction1] -= 20; }
                }},
                {"Report to authorities", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction1] += 15;
                }}
            }
        },

        // 6. Spiritual & Lore Events
        {
            "Holytacta Meditation",
            "An old master teaches focus through silence. The world falls away.",
            {
                {"Meditate (+Will, +Skill)", [](GameContext& ctx){
                }},
                {"Sleep instead", [](GameContext& ctx){
                }}
            }
        },
        {
            "Druid Circle",
            "Nature guardians gather. They offer protection for the wild places.",
            {
                {"Join them (+Rep, Nature)", [](GameContext& ctx){
                }},
                {"Harm the trees", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->reputation[(size_t)FactionID::Wilderness] -= 30; }
                    trigger_fight(ctx, NPCType::Woodcutter, "Druid Protector");
                }}
            }
        },
        {
            "Ancient Library",
            "Shelves of forbidden knowledge from the age of Sainthood. Tomes about the dead gods.",
            {
                {"Read about gods (+Knowledge)", [](GameContext& ctx){
                }},
                {"Burn the heresy", [](GameContext& ctx){
                }},
                {"Steal a tome (Black Magic)", [](GameContext& ctx){
                }}
            }
        },

        // 7. War & Conflict
        {
            "Battlefield of Yesterday",
            "Skeletons and rusted blades. A great battle happened here. Who won?",
            {
                {"Search for survivors", [](GameContext& ctx){
                }},
                {"Loot the dead (+Rep Loss)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->inventory.add(ResourceType::Iron, 2); p->reputation[(size_t)FactionID::Faction1] -= 5; }
                }}
            }
        },
        {
            "Refugee Camp",
            "Families flee the conflicts between kingdoms. Children cry for lost homes.",
            {
                {"Give aid (+Rep)", [](GameContext& ctx){
                }},
                {"Rob them (+Gold, -Rep)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->inventory.add_capital(40); p->reputation[(size_t)FactionID::Faction1] -= 20; }
                }},
                {"Recruit them (Soldiers)", [](GameContext& ctx){
                }}
            }
        },
        {
            "Spy for the Evnuchs",
            "An agent offers gold for information about mages in the region.",
            {
                {"Accept (Gold, -Rep with Mages)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->inventory.add_capital(60); p->reputation[(size_t)FactionID::Faction2] -= 15; }
                }},
                {"Refuse", [](GameContext&){}},
                {"Double-cross (Warn mages)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction2] += 15;
                }}
            }
        },

        // 8. Lore Mystery Events
        {
            "Sainthood's Relic",
            "An ancient artifact glows with Sainthood's touch. Used to bind demons and shadow.",
            {
                {"Use it against evil", [](GameContext& ctx){
                }},
                {"Sell to cult (Dark Power)", [](GameContext& ctx){
                }}
            }
        },
        {
            "The Black Spear",
            "Legend speaks of a spear that pierced archmagic itself. Rumor: the Czar carried it.",
            {
                {"Search for it", [](GameContext& ctx){
                }},
                {"Is it real?", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding);
                }}
            }
        },
        {
            "Shadow Child Prophecy",
            "Cultists chant: 'When the peasant pierces the royal blood, the shadows shall wake.'",
            {
                {"Learn the prophecy (+Lust)", [](GameContext& ctx){
                }},
                {"Stop them!", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Bandit, "Cult Believer");
                }},
                {"Ignore", [](GameContext&){}}
            }
        },

        // 9. Personal Encounters
        {
            "The Lonely Elf",
            "She says she was alive when Sainthood ruled. She remembers everything.",
            {
                {"Ask her stories (+Skill)", [](GameContext& ctx){
                }},
                {"Seduce her (+Lust)", [](GameContext& ctx){
                }},
                {"Leave", [](GameContext&){}}
            }
        },
        {
            "A Magus in Hiding",
            "An old mage lives secretly, fearing the Paladins.",
            {
                {"Protect him (+Rep with mages)", [](GameContext& ctx){
                }},
                {"Betray him (+Gold)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->inventory.add_capital(50); p->reputation[(size_t)FactionID::Faction2] -= 25; }
                }},
                {"Learn from him (+Skill)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) { p->learn_skill(SkillID::Bodybuilding); p->reputation[(size_t)FactionID::Faction1] -= 5; }
                }}
            }
        },
        {
            "The Wanderer",
            "A mysterious figure with a black spear. Your heart races. Is it... him?",
            {
                {"Challenge him", [](GameContext& ctx){
                    trigger_fight(ctx, NPCType::Guard, "The Czar-Peasant");
                }},
                {"Bow in respect", [](GameContext& ctx){
                }},
                {"Ask his name", [](GameContext& ctx){
                }}
            }
        },

        // 10. Quick Lore Fluff
        {"Curse the Empire", "Someone mutters curses. Dangerous talk.", {{"Listen", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Faction2]+=2; }}}},
        {"Sacred and Profane", "A temple and a shrine stand back-to-back.", {{"Question faith", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Bodybuilding); }}}},
    };

    return DB;
}

const RandomEvent& get_random_event_data(int id) {
    const auto& db = get_event_db();
    if (id < 0 || id >= static_cast<int>(db.size())) {
        return db[0];
    }
    return db[id];
}

int get_random_event_count() {
    return static_cast<int>(get_event_db().size());
}
