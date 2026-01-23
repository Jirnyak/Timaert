#include "systems/random_events.h"
#include "systems/world_manager.h"
#include "states/battle_state.h"
#include <cstring>
#include <algorithm>

Player* get_player(GameContext& ctx) {
    if (ctx.world_manager) {
        return &ctx.world_manager->player_ctrl.player();
    }
    return nullptr;
}

void trigger_fight(GameContext& ctx, NPCType type, const std::string& override_name) {
    if (!ctx.world_manager) return;
    
    Player* p = get_player(ctx);
    if (!p) return;

    NPC* enemy = ctx.world_manager->npcs.spawn(type, p->pos, -1, ctx.rng);
    if (enemy) {
        if (!override_name.empty()) {
            std::strncpy(enemy->name, override_name.c_str(), sizeof(enemy->name) - 1);
        }
        std::strncpy(enemy->personality, "Aggressive", sizeof(enemy->personality) - 1);
        
        push_state(ctx, std::make_unique<BattleState>(enemy->id));
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
                    if (auto* p = get_player(ctx)) p->life = std::min(p->life + 30, p->max_life);
                }},
                {"Loot (Gold)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->inventory.add_capital(40);
                        p->reputation[(size_t)FactionID::Kingdom] -= 2;
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
                        p->life = std::min(p->life + 10, p->max_life);
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
                        p->inventory.add(ResourceType::Wood, 3);
                        p->inventory.add(ResourceType::Cloth, 2);
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
                    if (auto* p = get_player(ctx)) p->will = std::max(0, p->will - 10);
                }},
                {"Find Shelter (-Time)", [](GameContext& ctx) {
                     ctx.hour += 2; // Ждем 2 часа
                }}
            }
        },
        {
            "Sunny Glade",
            "The sun shines brightly on this peaceful patch of grass.",
            {
                {"Rest (+Will)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will = std::min(p->max_will, p->will + 15);
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
                            p->life = std::min(p->life + 50, p->max_life);
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
                            p->reputation[(size_t)FactionID::Kingdom] += 2;
                        }
                    }
                }},
                {"Kick him (-Rep)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Kingdom] -= 5;
                        p->will = std::max(0, p->will - 5);
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
                    if (auto* p = get_player(ctx)) p->reputation[(size_t)FactionID::Kingdom] += 10;
                }},
                {"Ignore (-Will)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will -= 5;
                }}
            }
        },
        {
            "Bard",
            "A bard offers to sing a song of your deeds.",
            {
                {"Listen (+Will)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will += 10;
                }},
                {"Tip 20g (+Rep)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                         if (p->inventory.get_capital() >= 20) {
                             p->inventory.remove_capital(20);
                             p->reputation[(size_t)FactionID::Kingdom] += 5;
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
                    if (auto* p = get_player(ctx)) p->will -= 20;
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
                    if (auto* p = get_player(ctx)) p->life -= 15;
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
                    if (auto* p = get_player(ctx)) p->reputation[(size_t)FactionID::Kingdom] -= 5;
                }}
            }
        },

        // --- 15. Lore & Atmosphere ---
        {
            "Ancient Ruins",
            "Stones marked with forgotten runes jut from the earth.",
            {
                {"Examine", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will += 5; // Knowledge inspires
                }},
                {"Touch", [](GameContext& ctx) {
                     if (auto* p = get_player(ctx)) {
                         p->life -= 10; // Shock
                         p->max_will += 5; // Permanent boost?
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
                        p->will -= 5; // Disgusting
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
                        p->lust += 20;
                    }
                }},
                {"Destroy", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will -= 10; // Bad luck
                }}
            }
        },
        {
            "Shooting Star",
            "A bright streak across the night sky.",
            {
                {"Make a wish (+Will)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will = p->max_will;
                }}
            }
        },
        {
            "Battlefield",
            "Old rusted armor and bones scatter the field.",
            {
                {"Scavenge", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->inventory.add(ResourceType::Iron, 5);
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
                        p->life = p->max_life;
                        p->will += 10;
                    }
                }},
                {"Play with self (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->lust += 25;
                        p->will -= 10;
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
                        p->lust += 20;
                        p->will -= 5;
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
                    if (auto* p = get_player(ctx)) p->will -= 5; // Embarrassed
                }},
                {"Embrace it (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->lust += 15;
                        p->learn_skill(SkillID::FlashPanty);
                    }
                }}
            }
        },
        {
            "Aphrodisiac Pollen",
            "You walk through a field of strange pink flowers. The scent is heady.",
            {
                {"Hold breath", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will -= 10;
                }},
                {"Inhale (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->lust += 40;
                        p->will -= 20;
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
                        p->lust = p->max_lust; // INSTANT HORNY
                        p->life += 20;
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
                        p->lust += 10;
                        p->learn_skill(SkillID::Wink);
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
                     if (auto* p = get_player(ctx)) p->life -= 5;
                }},
                {"Let it linger (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->lust += 30;
                        p->will -= 15;
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
                        p->lust += 10;
                        p->learn_skill(SkillID::Tease);
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
                        p->lust += 15;
                        p->will -= 5;
                    }
                }},
                {"Burn it", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will += 5;
                }}
            }
        },
        {
            "Succubus Whisper",
            "A voice in your head suggests naughty things.",
            {
                {"Resist (-Will)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will -= 10;
                }},
                {"Give in (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->lust += 20;
                        p->learn_skill(SkillID::Moan);
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
                        p->will = 0; // Broken
                        p->lust += 50;
                        p->learn_skill(SkillID::StripFull);
                        p->reputation[(size_t)FactionID::Outlaws] += 5; // They like you now
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
                        p->life -= 10;
                        p->lust += 10;
                    }
                }},
                {"Enjoy (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->lust = p->max_lust;
                        p->will /= 2;
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
                        p->lust += 30; // And dignity
                        p->will -= 30;
                    }
                }}
            }
        },
        {
            "Hypnotic Dancer",
            "A dancer moves in a way that captures your mind.",
            {
                {"Look away", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) p->will -= 10;
                }},
                {"Stare (+Lust)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->lust += 25;
                        p->learn_skill(SkillID::Stare);
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
                        p->lust += 20;
                        p->learn_skill(SkillID::TouchSelf);
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
                        if(p->inventory.capital >= 50) {
                            p->inventory.capital -= 50;
                            p->reputation[(size_t)FactionID::Kingdom] += 2;
                        } else {
                            p->reputation[(size_t)FactionID::Kingdom] -= 5;
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
                        p->reputation[(size_t)FactionID::Outlaws] += 10;
                        p->reputation[(size_t)FactionID::Kingdom] -= 10;
                    }
                }},
                {"No (Report him)", [](GameContext& ctx) {
                    if (auto* p = get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Outlaws] -= 10;
                        p->reputation[(size_t)FactionID::Kingdom] += 5;
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
                        p->reputation[(size_t)FactionID::Kingdom] -= 5;
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
        {"Good Omen", "A white bird flies overhead.", {{"Smile", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will+=5; }}}},
        {"Bad Omen", "A black cat crosses your path.", {{"Frown", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will-=5; }}}},
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
        {"Wet Dream", "You wake up sticky.", {{"Clean up (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=10; }}}},
        {"Naughty Thought", "A random lewd thought.", {{"Blush", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Wardrobe Malfunction", "Button pops.", {{"Fix", [](GameContext&){}}}},
        {"Gaze", "Someone is staring at your ass.", {{"Wiggle (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Heat", "It's getting hot...", {{"Sweat", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=2; }}}},
        {"Tight Clothes", "Clothes feel tight.", {{"Adjust", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=2; }}}},
        {"Breeze", "Wind under your clothes.", {{"Shiver", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Erotic Art", "Carved on a tree.", {{"Inspect", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=10; }}}},
        {"Moan?", "Did you hear that?", {{"Listen", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Sticky Sap", "Sticky stuff on a tree.", {{"Touch", [](GameContext&){}}}}, // 68

        // Specific Skills Learning
        {"Old Master", "A monk offers to teach you to focus.", {{"Learn (+Focus)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Focus); }}}},
        {"Street Fighter", "Teaches you a dirty trick.", {{"Learn (+Dirty Blow)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::DirtyBlow); }}}},
        {"Courtesan", "She teaches you to wink.", {{"Learn (+Wink)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Wink); }}}},
        
        // Items
        {"Rusty Sword", "Lying in the grass.", {{"Take", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 1); }}}},
        {"Bag of Salt", "Spilled but usable.", {{"Scrape up", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Salt, 2); }}}},
        {"Wine Bottle", "Half full.", {{"Drink (+Will, +Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->will+=5; p->lust+=5; } }}}},
        {"Silk Scarf", "Smells of perfume.", {{"Keep", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Cloth, 1); }}}},
        {"Gold Nugget", "Lucky!", {{"Rich!", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.capital+=100; }}}},
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
                        if(p->inventory.capital >= 50) {
                            p->inventory.capital -= 50;
                            p->lust = 0; // Release
                            p->will = std::min(p->max_will, p->will + 20);
                        }
                    }
                }},
                {"Sell Body (+Gold, -Will)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->inventory.capital += 40;
                        p->will = std::max(0, p->will - 25);
                        p->lust += 10;
                        p->reputation[(size_t)FactionID::Kingdom] -= 5;
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
                    if(auto* p=get_player(ctx)) p->will += 15;
                }},
                {"Steal Food", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->inventory.add(ResourceType::Grain, 5);
                        p->reputation[(size_t)FactionID::Kingdom] -= 2;
                    }
                }},
                {"Get Drunk (Wine)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->inventory.add(ResourceType::Wine, 1);
                        p->will += 5;
                    }
                }}
            }
        },
        {
            "Public Execution",
            "A criminal is being hanged in the square.",
            {
                {"Watch (-Will)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->will -= 10;
                }},
                {"Save him!", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->reputation[(size_t)FactionID::Kingdom] -= 50;
                        p->reputation[(size_t)FactionID::Outlaws] += 50;
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
                            p->life = p->max_life;
                            p->lust += 30; // Seeing others
                        }
                    }
                }},
                {"Peep (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->lust += 15;
                        p->learn_skill(SkillID::Stare);
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
                        p->max_life += 5;
                        p->life -= 10;
                        p->lust += 20; // Strange side effects
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
                    if(auto* p=get_player(ctx)) p->life -= 25;
                }}
            }
        },
        {
            "Hermit's Shack",
            "An old man lives here. He looks crazy.",
            {
                {"Talk", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->learn_skill(SkillID::Meditate);
                        p->will += 20;
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
                    if(auto* p=get_player(ctx)) p->will += 10;
                }},
                {"Worship (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->lust += 50;
                        p->will -= 20;
                        p->learn_skill(SkillID::BegForMercy);
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
                        p->lust += 5;
                        p->will += 5; // Relief
                    }
                }},
                {"Suffer (-Will)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->will -= 5;
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
                        p->lust += 40;
                        p->will -= 10;
                        // Simulating slime attack
                        p->learn_skill(SkillID::SlimeTrap);
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
                        p->lust += 15;
                        p->learn_skill(SkillID::FlashPanty);
                    }
                }},
                {"Laugh (+Will)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->will += 5;
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
                        p->lust += 45;
                        p->will -= 20;
                        p->learn_skill(SkillID::TentacleSummon);
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
                        p->will = 10; // Drained
                        p->lust += 20;
                        p->learn_skill(SkillID::HypnoStare);
                    }
                }},
                {"Take it (+Gold)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->inventory.capital += 30;
                }}
            }
        },
        {
            "Aphrodisiac Gas",
            "A pink cloud erupts from the ground!",
            {
                {"Breathe (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->lust = p->max_lust;
                        p->will -= 30;
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
                        p->lust += 25;
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
                        p->max_life += 20;
                        p->lust += 20; // Tight!
                    }
                }},
                {"Sell (20g)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->inventory.capital += 20;
                }}
            }
        },
        {
            "Nude Beach",
            "Locals are swimming naked here.",
            {
                {"Join them (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->lust += 30;
                        p->will += 10;
                        p->learn_skill(SkillID::StripFull);
                    }
                }},
                {"Watch from bushes", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->lust += 15;
                }}
            }
        },
        {
            "Oil Bottle",
            "Slippery massage oil.",
            {
                {"Use on self (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->lust += 20;
                        p->learn_skill(SkillID::Massage);
                    }
                }},
                {"Drink? (Sick)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->life -= 5;
                }}
            }
        },
        {
            "Bound Person",
            "Someone is tied up. They look... aroused?",
            {
                {"Untie", [](GameContext& ctx){
                     if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Kingdom] += 2;
                }},
                {"Tease (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->lust += 20;
                        p->reputation[(size_t)FactionID::Outlaws] += 2;
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
                    if(auto* p=get_player(ctx)) p->will -= 15;
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
                        if(p->inventory.capital >= 20) p->inventory.capital -= 20;
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
                        p->will += 5;
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
                        p->inventory.capital = 0;
                        p->lust += 50;
                        p->will = 0;
                        p->learn_skill(SkillID::BegForMercy);
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
                        p->learn_skill(SkillID::Kick);
                        p->will += 5;
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
                        p->learn_skill(SkillID::SeductivePose);
                        p->lust += 10;
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
                        p->learn_skill(SkillID::Grope);
                        p->reputation[(size_t)FactionID::Kingdom] -= 1;
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
                        p->learn_skill(SkillID::Fireball);
                        p->will -= 10;
                    }
                }},
                {"Read Erotica (+Lust)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) p->lust += 15;
                }}
            }
        },
        {
            "Gym",
            "Weights made of stone.",
            {
                {"Lift (+HP)", [](GameContext& ctx){
                    if(auto* p=get_player(ctx)) {
                        p->max_life += 2;
                        p->will -= 10;
                    }
                }}
            }
        },

        // --- MISC / FLUFF (Quick Events) ---
        {"Nice View", "A beautiful sunset.", {{"Watch", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will+=5; }}}},
        {"Bad Smell", "Eww.", {{"Cover nose", [](GameContext&){}}}},
        {"Butterfly", "It lands on your nose.", {{"Sneeze", [](GameContext&){}}}},
        {"Loose Rock", "You almost tripped.", {{"Curse", [](GameContext&){}}}},
        {"Bird Poop", "Right on your shoulder.", {{"Clean (+Will)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will-=2; }}}},
        {"Coin on ground", "A copper piece.", {{"Take (1g)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.capital+=1; }}}},
        {"Cat", "Meow.", {{"Pet", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will+=5; }}}},
        {"Dog", "Woof.", {{"Pet", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will+=5; }}}},
        {"Rain", "Again?", {{"Sigh", [](GameContext&){}}}},
        {"Rainbow", "Somewhere over it...", {{"Smile", [](GameContext&){}}}},
        
        {"Broken Wheel", "Someone abandoned a cart part.", {{"Take Wood", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Wood, 1); }}}},
        {"Wild Apple", "A sour apple.", {{"Eat", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life+=2; }}}},
        {"Ant Hill", "Don't sit there.", {{"Poke", [](GameContext&){}}}},
        {"Spider", "A tiny one.", {{"Squish", [](GameContext&){}}}},
        {"Snail", "Slowly moving.", {{"Watch", [](GameContext&){}}}},
        {"Cloud shape", "Looks like a butt.", {{"Giggle (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=2; }}}},
        {"Echo", "Hello!", {{"Shout", [](GameContext&){}}}},
        {"Puddle", "Splash.", {{"Jump", [](GameContext&){}}}},
        {"Flower", "Smells nice.", {{"Sniff", [](GameContext&){}}}},
        {"Bee", "Buzz off.", {{"Run", [](GameContext&){}}}},

        {"Hiccups", "You can't stop.", {{"Hold breath", [](GameContext&){}}}},
        {"Itchy Back", "Can't reach it.", {{"Rub on tree", [](GameContext&){}}}},
        {"Loose Button", "Pop.", {{"Fix", [](GameContext&){}}}},
        {"Hole in pocket", "Lost a coin.", {{"Damn (-1g)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.capital-=1; }}}},
        {"Nice Breeze", "Refreshes you.", {{"Enjoy", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will+=2; }}}},
        {"Scary Shadow", "Just a tree.", {{"Phew", [](GameContext&){}}}},
        {"Owl", "Hoot hoot.", {{"Listen", [](GameContext&){}}}},
        {"Firefly", "Glows in dark.", {{"Catch", [](GameContext&){}}}},
        {"Shoe Stone", "Annoying.", {{"Remove", [](GameContext&){}}}},
        {"Yawn", "Contagious.", {{"Sleepy", [](GameContext&){}}}},

        // --- MORE FETISH / LUST ---
        {"Upskirt Gust", "Wind blows your clothes up.", {{"Blush (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Wet Clothes", "Rain made them transparent.", {{"Hide", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will-=2; }}, {"Show off", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=10; }}}},
        {"Tight Pants", "Hard to walk.", {{"Adjust", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=2; }}}},
        {"Sweaty", "You are dripping.", {{"Wipe", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=2; }}}},
        {"Heavy Breathing", "From behind a bush.", {{"Check", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Lingerie Shop", "Display window.", {{"Look (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Dirty Talk", "Overheard lovers.", {{"Listen (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=10; }}}},
        {"Nude Statue", "Artistic.", {{"Touch", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Cream Pie", "A bakery selling pies.", {{"Eat", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life+=5; }}}},
        {"Banana", "Just a fruit.", {{"Eat suggestively", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},

        {"Rope", "Useful for binding.", {{"Keep (+Rope)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Cloth, 1); }}}},
        {"Gag", "A cloth gag.", {{"Try it on (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=15; }}}},
        {"Whip", "For horses, right?", {{"Keep", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Spank); }}}},
        {"Blindfold", "Can't see.", {{"Wear (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=10; }}}},
        {"Collar", "Leather collar.", {{"Wear (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=20; }}}},
        {"Vibrating Stone", "Magic artifact.", {{"Keep", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Porn Magazine", "Drawings.", {{"Read", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=10; }}}},
        {"Love Letter", "Very explicit.", {{"Read", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust+=5; }}}},
        {"Condom", "Primitive protection.", {{"Take", [](GameContext&){}}}},

        // --- EXPANSION PACK: ADVENTURE & 18+ ---

        // 1. Wild Encounters
        {"Wild Horse", "A magnificent stallion grazes nearby.", {
            {"Ride (+Travel)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->move_progress += 50; p->will += 5; } }},
            {"Ignore", [](GameContext&){}}
        }},
        {"Bear Trap", "Hidden in the leaves.", {
            {"Disarm (+Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 1); }},
            {"Step in (-HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life -= 20; }}
        }},
        {"Giant Spider", "It drops from a tree!", {
            {"Fight", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Giant Spider"); }},
            {"Run (-Will)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will -= 10; }}
        }},
        {"Injured Wolf", "It whines in pain.", {
            {"Heal (+Rep)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->reputation[(size_t)FactionID::Wilderness] += 5; p->will += 5; } }},
            {"Kill (+Food)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Grain, 1); }}
        }},
        {"Bee Hive", "Full of honey.", {
            {"Take Honey (+Food, -HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->inventory.add(ResourceType::Grain, 2); p->life -= 5; } }},
            {"Leave", [](GameContext&){}}
        }},

        // 2. Mystical & Magic
        {"Glowing Rune", "Carved into a rock. It hums.", {
            {"Touch (+Mana/Will)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->will = p->max_will; p->life -= 5; } }},
            {"Study (+Int)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Meditate); }}
        }},
        {"Magic Mirror", "Reflects your deepest desires.", {
            {"Gaze (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->lust += 30; p->will -= 10; } }},
            {"Smash", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will += 10; }}
        }},
        {"Cursed Sword", "A black blade stuck in the ground.", {
            {"Take (-HP, +Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->life -= 15; p->inventory.add(ResourceType::Iron, 3); } }},
            {"Leave", [](GameContext&){}}
        }},
        {"Fountain of Youth", "The water sparkles.", {
            {"Drink (Heal)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life = p->max_life; }},
            {"Bottle it", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Wine, 1); }}
        }},
        {"Ghost", "A transparent figure points somewhere.", {
            {"Follow (+Gold)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->inventory.capital += 25; p->will -= 5; } }},
            {"Exorcise", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::HolySmite); }}
        }},

        // 3. Social & City
        {"Card Game", "Locals invite you to play.", {
            {"Play (-10g, 50% chance +30g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if(p->inventory.capital >= 10) {
                        p->inventory.capital -= 10;
                        if(rand()%2 == 0) p->inventory.capital += 30;
                    }
                }
            }},
            {"Cheating (+Skill)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Wink); }}
        }},
        {"Pickpocket", "Someone bumps into you.", {
            {"Check Pockets", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { if(rand()%2==0) p->inventory.capital -= 5; } }},
            {"Catch him!", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Thief"); }}
        }},
        {"Drunkard", "He offers you a drink.", {
            {"Drink (+Will, -Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->will += 5; p->lust -= 5; } }},
            {"Decline", [](GameContext&){}}
        }},
        {"Preacher", "He shouts about doom.", {
            {"Listen (-Will)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will -= 5; }},
            {"Argue", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->reputation[(size_t)FactionID::Kingdom] -= 1; }}
        }},
        {"Lost Dog", "Looks hungry.", {
            {"Feed (+Rep)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->reputation[(size_t)FactionID::Kingdom] += 2; p->will += 5; } }},
            {"Shoo", [](GameContext&){}}
        }},

        // 4. Erotic / 18+ Scenarios
        {"Hot Spring (Mixed)", "Men and women bathing together.", {
            {"Join (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->lust += 25; p->life += 20; } }},
            {"Watch (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->lust += 15; p->learn_skill(SkillID::Stare); } }}
        }},
        {"Succubus Trap", "A beautiful woman calls for help.", {
            {"Help", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) { p->lust += 50; p->will -= 20; } 
                trigger_fight(ctx, NPCType::Witch, "Succubus");
            }},
            {"Ignore", [](GameContext&){}}
        }},
        {"Torn Dress", "A woman's dress is caught on a bush.", {
            {"Help (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->lust += 10; p->reputation[(size_t)FactionID::Kingdom] += 5; } }},
            {"Stare", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 20; }}
        }},
        {"Aphrodisiac Merchant", "Sells special potions.", {
            {"Buy Potion (50g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if(p->inventory.capital >= 50) {
                        p->inventory.capital -= 50;
                        p->max_lust += 10;
                    }
                }
            }},
            {"Ask for demo (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 15; }}
        }},
        {"Magic Bindings", "You step into a magical snare.", {
            {"Struggle (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->lust += 30; p->life -= 5; } }},
            {"Wait (-Will)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will -= 10; }}
        }},
        {"Nude Statue", "Carved with incredible detail.", {
            {"Touch (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 10; }},
            {"Admire", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will += 5; }}
        }},
        {"Peeping Goblin", "Watching you pee.", {
            {"Kick him", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Peeping Goblin"); }},
            {"Show off (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->lust += 20; p->learn_skill(SkillID::Tease); } }}
        }},
        {"Wet Clothes", "Rain makes your clothes see-through.", {
            {"Cover up", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will -= 5; }},
            {"Flaunt it (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 15; }}
        }},
        {"Massage Parlor", " signs promise 'Happy Endings'.", {
            {"Enter (100g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if(p->inventory.capital >= 100) {
                        p->inventory.capital -= 100;
                        p->lust = 0; 
                        p->will = p->max_will;
                    }
                }
            }},
            {"Work there (+Gold)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    p->inventory.capital += 50;
                    p->lust += 20;
                    p->reputation[(size_t)FactionID::Kingdom] -= 5;
                }
            }}
        }},
        {"Tentacle Monster", "Slimy appendages emerge!", {
            {"Fight", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Tentacle Beast"); }},
            {"Submit (Game Over?)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) { 
                    p->lust = p->max_lust; 
                    p->will = 0; 
                    p->life -= 10;
                } 
            }}
        }},

        // 5. Encounters & Loot
        {"Abandoned Camp", "Embers still warm.", {
            {"Search", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Wood, 2); }},
            {"Rest", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life += 10; }}
        }},
        {"Broken Cart", "Merchandise scattered.", {
            {"Loot (+Cloth)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Cloth, 3); }},
            {"Fix (+Rep)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->reputation[(size_t)FactionID::Kingdom] += 5; p->will -= 5; } }}
        }},
        {"Dead Adventurer", "Clutched a map.", {
            {"Take Map (Nothing)", [](GameContext&){}},
            {"Loot Gear (+Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 1); }}
        }},
        {"Wild Berries", "Red and juicy.", {
            {"Eat (+HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life += 5; }},
            {"Collect", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Grain, 1); }}
        }},
        {"Ancient Obelisk", "Covered in moss.", {
            {"Clean", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->will += 10; p->learn_skill(SkillID::Focus); } }},
            {"Ignore", [](GameContext&){}}
        }},

        // 6. Risky Business
        {"Slave Auction", "Humans for sale.", {
            {"Buy Slave (200g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if(p->inventory.capital >= 200) {
                        p->inventory.capital -= 200;
                        p->reputation[(size_t)FactionID::Outlaws] += 10;
                        // Mechanics for owning slave not implemented, just stat change
                        p->lust += 20;
                    }
                }
            }},
            {"Attack Slavers", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Bandit, "Slaver Boss"); }}
        }},
        {"Strange Potion", "Label says 'Growth'.", {
            {"Drink (+Str?)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->max_life += 10; p->life -= 10; } }},
            {"Sell (50g)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.capital += 50; }}
        }},
        {"Gambling Den", "Smoke and dice.", {
            {"Bet High (100g)", [](GameContext& ctx){ 
                if(auto* p=get_player(ctx)) {
                    if (p->inventory.capital >= 100) {
                        p->inventory.capital -= 100;
                        if(rand()%3 == 0) p->inventory.capital += 300;
                    }
                }
            }},
            {"Leave", [](GameContext&){}}
        }},
        {"Witch's Brew", "Smells like lust.", {
            {"Drink (+Lust, -Will)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->lust = p->max_lust; p->will -= 20; } }},
            {"Spill", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Witch, "Angry Witch"); }}
        }},
        {"Magic Mushroom", "Colors are melting.", {
            {"Eat (+Will, -HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->will += 30; p->life -= 10; } }},
            {"Ignore", [](GameContext&){}}
        }},

        // 7. Random Fluff (Quick)
        {"Sunny Day", "Birds are singing.", {{"Smile", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will += 2; }}}},
        {"Rainy Night", "Cold and wet.", {{"Shiver", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will -= 2; }}}},
        {"Shooting Star", "Make a wish.", {{"Wish (+Will)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will = p->max_will; }}}},
        {"Breeze", "Wind lifts your skirt/cloak.", {{"Blush", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 5; }}}},
        {"Itch", "Mosquito bite.", {{"Scratch", [](GameContext&){}}}},
        {"Sneeze", "Dust.", {{"Bless you", [](GameContext&){}}}},
        {"Stumble", "Tripped on a root.", {{"Ouch (-1 HP)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life -= 1; }}}},
        {"Lost Coin", "Found 1 gold.", {{"Take", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.capital += 1; }}}},
        {"Beautiful Flower", "Smells sweet.", {{"Sniff", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will += 2; }}}},
        {"Ugly Bug", "Eww.", {{"Squish", [](GameContext&){}}}},
        {"Distant Howl", "Scary.", {{"Hurry", [](GameContext&){}}}},
        {"Old Boot", "Trash.", {{"Ignore", [](GameContext&){}}}},
        {"Rainbow", "Pretty.", {{"Watch", [](GameContext&){}}}},
        {"Cloud Shape", "Looks obscene.", {{"Giggle (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 2; }}}},
        {"Deja Vu", "Have I been here?", {{"Confused", [](GameContext&){}}}},
        {"Hunger", "Tummy rumbles.", {{"Eat ration", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.remove(ResourceType::Grain, 1); }}}},
        {"Thirst", "Mouth dry.", {{"Drink", [](GameContext&){}}}},
        {"Tired", "Need sleep.", {{"Yawn", [](GameContext&){}}}},
        {"Bored", "Nothing happens.", {{"Hum", [](GameContext&){}}}},
        {"Silence", "Too quiet.", {{"Alert", [](GameContext&){}}}},

        // 8. Skill Trainers
        {"Old Monk", "Teaches discipline.", {
            {"Learn (+Meditate)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Meditate); }},
            {"Rob", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Peasant, "Monk"); }}
        }},
        {"Retired General", "Teaches war.", {
            {"Learn (+WarCry)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::WarCry); }},
            {"Duel", [](GameContext& ctx){ trigger_fight(ctx, NPCType::Guard, "General"); }}
        }},
        {"Seductress", "Teaches love.", {
            {"Learn (+Kiss)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->learn_skill(SkillID::Kiss); p->lust += 10; } }},
            {"Sleep with her", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->lust = 0; p->will = p->max_will; p->inventory.capital -= 20; } }}
        }},
        {"Thief Master", "Teaches stealth.", {
            {"Learn (+Backstab)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) { p->learn_skill(SkillID::Backstab); p->reputation[(size_t)FactionID::Kingdom] -= 5; } }}
        }},
        {"Mad Mage", "Mumbles spells.", {
            {"Learn (+Spark)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::Spark); }},
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
            {"Wear (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 15; }}
        }},
        {"Jewelry Box", "Locked.", {
            {"Break (+Gold)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.capital += 40; }},
            {"Pick lock", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->learn_skill(SkillID::DirtyBlow); }} // Learning by doing
        }},
        {"Strange Idol", "Vibrates.", {
            {"Touch (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 20; }},
            {"Sell (10g)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.capital += 10; }}
        }},

        // 10. Final Batch
        {"Flood", "River overflowed.", {{"Swim (-Stamina)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will -= 5; }}}},
        {"Landslide", "Rocks falling.", {{"Dodge", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->life -= 5; }}}},
        {"Forest Fire", "Smoke everywhere.", {{"Run!", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will -= 10; }}}},
        {"Earthquake", "Ground shakes.", {{"Hold on", [](GameContext&){}}}},
        {"Eclipse", "Day turns to night.", {{"Pray", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will += 10; }}}},
        {"Meteor", "Crashes nearby.", {{"Investigate (+Iron)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->inventory.add(ResourceType::Iron, 5); }}}},
        {"Aurora", "Beautiful lights.", {{"Watch (+Will)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will += 10; }}}},
        {"Snowstorm", "Freezing.", {{"Find shelter", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will -= 5; }}}},
        {"Heatwave", "Boiling.", {{"Strip (+Lust)", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->lust += 10; }}}},
        {"Perfect Day", "Everything goes right.", {{"Enjoy", [](GameContext& ctx){ if(auto* p=get_player(ctx)) p->will = p->max_will; }}}}
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
