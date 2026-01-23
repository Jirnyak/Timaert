#pragma once

#include <string>
#include <array>
#include <cstdint>
#include "core/types.h"

struct Skill
{
    std::string name;
    SkillType type;
    int power;        // Сила эффекта
    int cost;         // Стоимость (мана/выносливость)
    std::string description;
};

// Полный список ID навыков
enum class SkillID : std::int32_t
{
    // --- Unarmed / Basic (0-9) ---
    Punch = 0,
    Kick,
    Headbutt,
    Bite,
    Tackle,
    Slap,
    DirtyBlow,
    Struggle,
    Rest,
    Wait,

    // --- Weapon / Physical (10-24) ---
    Slash,
    Thrust,
    Bash,
    Cleave,
    DoubleStrike,
    HeavySwing,
    Pierce,
    ShieldBash,
    Parry,
    Disarm,
    Hamstring,
    Execute,
    Backstab,
    Whirlwind,
    Charge,

    // --- Magic: Elemental (25-49) ---
    Fireball,
    FireBreath,
    Inferno,
    IceShard,
    Freeze,
    Blizzard,
    Thunderbolt,
    Spark,
    ChainLightning,
    RockThrow,
    EarthSpike,
    Quake,
    Gust,
    Tornado,
    WaterJet,
    Tsunami,
    AcidSplash,
    PoisonCloud,
    VenomStrike,
    DrainLife,
    ShadowBolt,
    Darkness,
    HolySmite,
    LightBeam,
    Purify,

    // --- Healing / Support (50-59) ---
    Heal,
    GreatHeal,
    Regeneration,
    Bandage,
    IronSkin,
    WarCry,
    Meditate,
    Focus,
    Haste,
    Slow,

    // --- LUST: Tease / Flirt (60-74) ---
    Stare,            // Пристальный взгляд
    Wink,             // Подмигнуть
    BlowKiss,         // Воздушный поцелуй
    Moan,             // Стон
    Tease,            // Дразнить (показать тело)
    ShowBreasts,      // Показать грудь
    ShakeButt,        // Потрясти задом
    SeductivePose,    // Соблазнительная поза
    StripTop,         // Снять верх
    StripBottom,      // Снять низ
    StripFull,        // Раздеться полностью
    BegForMercy,      // Умолять (возбуждает садистов)
    Insult,           // Оскорбить (возбуждает мазохистов)
    FlashPanty,       // Засветить белье
    TouchSelf,        // Трогать себя

    // --- LUST: Foreplay / Soft Attack (75-89) ---
    Grope,            // Лапать
    Spank,            // Шлепнуть
    LickLips,         // Облизать губы
    Kiss,             // Поцелуй
    DeepKiss,         // Глубокий поцелуй
    NibbleEar,        // Кусь за ушко
    Massage,          // Массаж
    RubCrotch,        // Потереться промежностью
    SqueezeBoobs,     // Сжать грудь
    PinchNipples,     // Ущипнуть соски
    StrokeCock,       // Погладить член (против муж)
    RubClit,          // Потереть клитор (против жен)
    Finger,           // Пальцы
    Grind,            // Тереться телом
    Hump,             // Имитировать секс

    // --- LUST: Hard / Magical Sex (90-109) ---
    Pheromones,       // Феромоны (АОЕ возбуждение)
    AphrodisiacMist,  // Туман афродизиака
    TentacleSummon,   // Призыв щупалец
    SlimeTrap,        // Слизь-ловушка
    MindBreak,        // Ломка разума
    HypnoStare,       // Гипноз
    ForceOrgasm,      // Принудительный оргазм
    DrainLust,        // Выпить энергию (Суккуб)
    DeepThroat,       // Глубокий минет
    FaceSit,          // Сесть на лицо
    Cowgirl,          // Наездница
    DoggyStyle,       // Раком
    Missionary,       // Миссионерская
    AnalPound,        // Анал
    Creampie,         // Внутрь
    Swallow,          // Глотать
    Bukkake,          // На лицо
    GangbangSummon,   // Призыв помощи
    DoublePenetration,
    Orgy,

    Count
};

// База данных навыков
inline const Skill& get_skill_info(SkillID id)
{
    static const std::array<Skill, static_cast<size_t>(SkillID::Count)> SKILL_DB = {{
        // Basic
        {"Punch",       SkillType::Physical, 5,   0, "Weak punch."},
        {"Kick",        SkillType::Physical, 8,   2, "Standard kick."},
        {"Headbutt",    SkillType::Physical, 10,  5, "Risky attack."},
        {"Bite",        SkillType::Physical, 12,  0, "Savage bite."},
        {"Tackle",      SkillType::Physical, 5,   5, "Knock down enemy."},
        {"Slap",        SkillType::Physical, 3,   0, "Humiliating slap."},
        {"Dirty Blow",  SkillType::Physical, 15, 10, "Hit below the belt."},
        {"Struggle",    SkillType::Physical, 2,   5, "Try to escape grapple."},
        {"Rest",        SkillType::Heal,     5,   0, "Recover stamina."},
        {"Wait",        SkillType::Utility,  0,   0, "Do nothing."},

        // Weapon
        {"Slash",       SkillType::Physical, 20,  5, "Sword attack."},
        {"Thrust",      SkillType::Physical, 25,  8, "Piercing stab."},
        {"Bash",        SkillType::Physical, 22,  8, "Blunt force."},
        {"Cleave",      SkillType::Physical, 30, 15, "Hit multiple targets."},
        {"Double Strike",SkillType::Physical,35, 20, "Two fast hits."},
        {"Heavy Swing", SkillType::Physical, 40, 25, "Slow but strong."},
        {"Pierce",      SkillType::Physical, 20, 10, "Ignore armor."},
        {"Shield Bash", SkillType::Physical, 10, 10, "Stun enemy."},
        {"Parry",       SkillType::Support,  0,  10, "Block next hit."},
        {"Disarm",      SkillType::Physical, 5,  15, "Knock weapon away."},
        {"Hamstring",   SkillType::Physical, 15, 12, "Slow enemy down."},
        {"Execute",     SkillType::Physical, 80, 50, "Finish low HP enemy."},
        {"Backstab",    SkillType::Physical, 50, 20, "Sneak attack."},
        {"Whirlwind",   SkillType::Physical, 45, 40, "Spin to win."},
        {"Charge",      SkillType::Physical, 20, 15, "Rush forward."},

        // Magic Elemental
        {"Fireball",    SkillType::Magic,    30, 20, "Classic fire spell."},
        {"Fire Breath", SkillType::Magic,    25, 15, "Close range fire."},
        {"Inferno",     SkillType::Magic,    60, 50, "Massive fire damage."},
        {"Ice Shard",   SkillType::Magic,    25, 15, "Sharp ice projectile."},
        {"Freeze",      SkillType::Magic,    10, 25, "Stop enemy movement."},
        {"Blizzard",    SkillType::Magic,    50, 50, "Area ice storm."},
        {"Thunderbolt", SkillType::Magic,    35, 25, "Fast lightning."},
        {"Spark",       SkillType::Magic,    10,  5, "Tiny shock."},
        {"Chain Light.",SkillType::Magic,    45, 40, "Bounce between targets."},
        {"Rock Throw",  SkillType::Physical, 20, 10, "Throw a stone."},
        {"Earth Spike", SkillType::Magic,    35, 25, "Spike from below."},
        {"Quake",       SkillType::Magic,    50, 60, "Shake the ground."},
        {"Gust",        SkillType::Magic,    10, 10, "Push back."},
        {"Tornado",     SkillType::Magic,    45, 45, "Wind damage."},
        {"Water Jet",   SkillType::Magic,    20, 15, "High pressure water."},
        {"Tsunami",     SkillType::Magic,    55, 60, "Wave of destruction."},
        {"Acid Splash", SkillType::Magic,    15, 15, "Melts armor/clothes."},
        {"Poison Cloud",SkillType::Magic,    10, 20, "Damage over time."},
        {"Venom Strike",SkillType::Physical, 25, 15, "Poisoned weapon."},
        {"Drain Life",  SkillType::Magic,    20, 30, "Steal HP."},
        {"Shadow Bolt", SkillType::Magic,    30, 20, "Dark energy."},
        {"Darkness",    SkillType::Magic,     0, 30, "Blind enemy."},
        {"Holy Smite",  SkillType::Magic,    40, 30, "Divine damage."},
        {"Light Beam",  SkillType::Magic,    35, 25, "Laser attack."},
        {"Purify",      SkillType::Heal,      0, 20, "Remove debuffs."},

        // Support
        {"Heal",        SkillType::Heal,     30, 20, "Restore HP."},
        {"Great Heal",  SkillType::Heal,     80, 60, "Restore lots of HP."},
        {"Regen",       SkillType::Heal,     10, 30, "Heal over time."},
        {"Bandage",     SkillType::Heal,     15,  0, "Stop bleeding."},
        {"Iron Skin",   SkillType::Support,   0, 25, "Increase Defense."},
        {"War Cry",     SkillType::Support,   0, 20, "Increase Attack."},
        {"Meditate",    SkillType::Support,   0,  0, "Regen mana."},
        {"Focus",       SkillType::Support,   0, 10, "Next hit crits."},
        {"Haste",       SkillType::Support,   0, 30, "Speed up."},
        {"Slow",        SkillType::Magic,     5, 20, "Slow down enemy."},

        // LUST: Tease
        {"Stare",       SkillType::Lust,      5,  0, "Make them uncomfortable."},
        {"Wink",        SkillType::Lust,      8,  0, "Playful gesture."},
        {"Blow Kiss",   SkillType::Lust,     12,  5, "Send some love."},
        {"Moan",        SkillType::Lust,     15, 10, "Aural stimulation."},
        {"Tease",       SkillType::Lust,     20, 15, "Hint at what's under."},
        {"Show Boobs",  SkillType::Lust,     35, 20, "Flash chest."},
        {"Shake Butt",  SkillType::Lust,     30, 20, "Hypnotic movement."},
        {"Sexy Pose",   SkillType::Lust,     15, 10, "Highlight curves."},
        {"Strip Top",   SkillType::Lust,     25, 20, "Remove upper clothes."},
        {"Strip Bot",   SkillType::Lust,     40, 30, "Remove lower clothes."},
        {"Strip Full",  SkillType::Lust,    100, 80, "Go completely nude."},
        {"Beg Mercy",   SkillType::Lust,     10,  0, "Submit dominance."},
        {"Insult",      SkillType::Lust,     10,  5, "You won't last 5 sec!"},
        {"Flash Panty", SkillType::Lust,     15, 10, "Oops, wind blew."},
        {"Touch Self",  SkillType::Lust,     25, 20, "Masturbate in public."},

        // LUST: Foreplay
        {"Grope",       SkillType::Lust,     15,  5, "Touch without consent."},
        {"Spank",       SkillType::Lust,     18, 10, "Strike the rear."},
        {"Lick Lips",   SkillType::Lust,     10,  0, "Hungry look."},
        {"Kiss",        SkillType::Lust,     20, 10, "Lip contact."},
        {"Deep Kiss",   SkillType::Lust,     35, 20, "With tongue."},
        {"Nibble Ear",  SkillType::Lust,     25, 15, "Sensitive spot."},
        {"Massage",     SkillType::Lust,     15, 10, "Relaxing touch."},
        {"Rub Crotch",  SkillType::Lust,     30, 20, "Frot."},
        {"Squeeze",     SkillType::Lust,     25, 15, "Firm grip."},
        {"Pinch",       SkillType::Lust,     20, 10, "Pain and pleasure."},
        {"Stroke Cock", SkillType::Lust,     40, 25, "Handjob action."},
        {"Rub Clit",    SkillType::Lust,     40, 25, "Finger action."},
        {"Finger",      SkillType::Lust,     45, 30, "Internal touch."},
        {"Grind",       SkillType::Lust,     35, 25, "Body friction."},
        {"Hump",        SkillType::Lust,     30, 20, "Dry humping."},

        // LUST: Hard / Magic
        {"Pheromones",  SkillType::Lust,     20, 30, "Arousal cloud."},
        {"Aphrodisiac", SkillType::Lust,     50, 50, "Chemical lust."},
        {"Tentacles",   SkillType::Lust,     40, 40, "Summon slimy friends."},
        {"Slime Trap",  SkillType::Lust,     30, 35, "Sticky situation."},
        {"Mind Break",  SkillType::Lust,     99, 90, "Remove will completely."},
        {"Hypno Stare", SkillType::Lust,     25, 25, "Obey me."},
        {"Force Orgasm",SkillType::Lust,     80, 60, "Magical climax."},
        {"Drain Lust",  SkillType::Lust,     30, 30, "Succubus feed."},
        {"DeepThroat",  SkillType::Lust,     50, 40, "Oral finisher."},
        {"Face Sit",    SkillType::Lust,     45, 35, "Suffocating pleasure."},
        {"Cowgirl",     SkillType::Lust,     55, 45, "Ride 'em."},
        {"Doggy",       SkillType::Lust,     55, 45, "From behind."},
        {"Missionary",  SkillType::Lust,     50, 40, "Vanilla sex."},
        {"Anal",        SkillType::Lust,     65, 55, "Wrong hole?"},
        {"Creampie",    SkillType::Lust,     70, 60, "Fill up."},
        {"Swallow",     SkillType::Lust,     40, 20, "Protein shot."},
        {"Bukkake",     SkillType::Lust,     60, 40, "Messy finish."},
        {"Gangbang",    SkillType::Lust,     90, 80, "Call friends."},
        {"Double Pen",  SkillType::Lust,     85, 75, "Two at once."},
        {"Orgy",        SkillType::Lust,    100,100, "Everyone joins."}
    }};

    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= static_cast<int>(SkillID::Count))
        return SKILL_DB[0];
        
    return SKILL_DB[idx];
}
