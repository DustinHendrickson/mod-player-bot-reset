#include "mod-player-bot-reset.h"
#include "ScriptMgr.h"
#include "Player.h"
#include "Common.h"
#include "Chat.h"
#include "Log.h"
#include "PlayerbotAIBase.h"
#include "Configuration/Config.h"
#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "AutoMaintenanceOnLevelupAction.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "Item.h"
#include "RandomPlayerbotMgr.h"
#include "ObjectAccessor.h"
#include "PlayerbotFactory.h"

#include <sstream>
#include <vector>
#include <string>
#include <algorithm>

// -----------------------------------------------------------------------------
// GLOBALS: Configuration Values
// -----------------------------------------------------------------------------
static uint8 g_ResetBotChancePercent = 100;
static bool  g_DebugMode             = false;
static bool  g_ScaledChance          = false;

static bool  g_RestrictResetByPlayedTime = false;
static uint32 g_MinTimePlayed             = 86400; // in seconds (1 Day)
static uint32 g_PlayedTimeCheckFrequency  = 60;    // in seconds (default check frequency)

// -----------------------------------------------------------------------------
// STRUCT FOR RESET PAIRS (Idea B)
// -----------------------------------------------------------------------------
struct ResetPair {
    uint8 triggerLevel;
    uint8 targetLevel;
};

static std::vector<ResetPair> g_ResetPairs;

// -----------------------------------------------------------------------------
// UTILITY FUNCTION: Split a string by a delimiter
// -----------------------------------------------------------------------------
static std::vector<std::string> SplitString(const std::string &s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

// -----------------------------------------------------------------------------
// LOAD CONFIGURATION USING sConfigMgr
// -----------------------------------------------------------------------------
static void LoadPlayerBotResetConfig()
{
    // Load existing configuration options
    uint8 maxLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("ResetBotLevel.MaxLevel", 80));
    if (maxLevel < 2 || maxLevel > 80)
    {
        LOG_ERROR("server.loading", "[mod-player-bot-reset] Invalid ResetBotLevel.MaxLevel value: {}. Using default value 80.", maxLevel);
        maxLevel = 80;
    }

    g_ResetBotChancePercent = static_cast<uint8>(sConfigMgr->GetOption<uint32>("ResetBotLevel.ResetChance", 100));
    if (g_ResetBotChancePercent > 100)
    {
        LOG_ERROR("server.loading", "[mod-player-bot-reset] Invalid ResetBotLevel.ResetChance value: {}. Using default value 100.", g_ResetBotChancePercent);
        g_ResetBotChancePercent = 100;
    }

    g_ScaledChance = sConfigMgr->GetOption<bool>("ResetBotLevel.ScaledChance", false);
    g_RestrictResetByPlayedTime = sConfigMgr->GetOption<bool>("ResetBotLevel.RestrictTimePlayed", false);
    g_MinTimePlayed             = sConfigMgr->GetOption<uint32>("ResetBotLevel.MinTimePlayed", 86400);
    g_PlayedTimeCheckFrequency  = sConfigMgr->GetOption<uint32>("ResetBotLevel.PlayedTimeCheckFrequency", 60);
    g_DebugMode   = sConfigMgr->GetOption<bool>("ResetBotLevel.DebugMode", false);

    // -------------------------------------------------------------------------
    // Load Reset Pairs from configuration (Idea B)
    // Expected format: "trigger:target" pairs separated by semicolons.
    // For example: "80:1" or "80:10;58:70"
    // For non-DK bots, target is applied directly.
    // For DK bots, if target is less than 55, they will be reset to 55.
    // -------------------------------------------------------------------------
    std::string resetPairsStr = sConfigMgr->GetOption<std::string>("ResetBotLevel.ResetPairs", "80:1");
    g_ResetPairs.clear();
    std::vector<std::string> pairStrings = SplitString(resetPairsStr, ';');
    for (const std::string& pairStr : pairStrings)
    {
        std::vector<std::string> values = SplitString(pairStr, ':');
        if (values.size() != 2)
        {
            LOG_ERROR("server.loading", "[mod-player-bot-reset] Invalid reset pair format: '{}'. Expected format 'trigger:target'.", pairStr);
            continue;
        }
        uint8 trigger = static_cast<uint8>(atoi(values[0].c_str()));
        uint8 target  = static_cast<uint8>(atoi(values[1].c_str()));
        if (trigger < 2 || trigger > 80)
        {
            LOG_ERROR("server.loading", "[mod-player-bot-reset] Invalid trigger level {} in reset pair '{}'. Valid range: 2-80.", trigger, pairStr);
            continue;
        }
        if (target < 1 || target > 80)
        {
            LOG_ERROR("server.loading", "[mod-player-bot-reset] Invalid target level {} in reset pair '{}'. Valid range: 1-80.", target, pairStr);
            continue;
        }
        g_ResetPairs.push_back({trigger, target});
    }
    if (g_ResetPairs.empty())
    {
        // If no valid reset pairs are found, use the default pair.
        g_ResetPairs.push_back({80, 1});
    }
    else
    {
        // Sort reset pairs in descending order by trigger level so the highest applicable pair is used first.
        std::sort(g_ResetPairs.begin(), g_ResetPairs.end(), [](const ResetPair &a, const ResetPair &b) {
            return a.triggerLevel > b.triggerLevel;
        });
    }

    // Log loaded reset pairs
    std::string pairsLog;
    for (const ResetPair &pair : g_ResetPairs)
    {
        pairsLog += std::to_string(pair.triggerLevel) + ":" + std::to_string(pair.targetLevel) + " ";
    }
    LOG_INFO("server.loading", "[mod-player-bot-reset] Loaded and active with reset pairs: {} ResetChance = {}%, ScaledChance = {}.",
             pairsLog, static_cast<int>(g_ResetBotChancePercent), g_ScaledChance ? "Enabled" : "Disabled");
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS: Detect if a Player is a Bot
// -----------------------------------------------------------------------------
static bool IsPlayerBot(Player* player)
{
    if (!player)
    {
        LOG_ERROR("server.loading", "[mod-player-bot-reset] IsPlayerBot called with nullptr.");
        return false;
    }
    PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(player);
    return botAI && botAI->IsBotAI();
}

static bool IsPlayerRandomBot(Player* player)
{
    if (!player)
    {
        LOG_ERROR("server.loading", "[mod-player-bot-reset] IsPlayerRandomBot called with nullptr.");
        return false;
    }
    return sRandomPlayerbotMgr->IsRandomBot(player);
}

// -----------------------------------------------------------------------------
// HELPER FUNCTION: Compute the Reset Chance Based on a Reset Pair
// -----------------------------------------------------------------------------
static uint8 ComputeResetChance(uint8 level, uint8 triggerLevel)
{
    uint8 chance = g_ResetBotChancePercent;
    if (g_ScaledChance)
    {
        chance = static_cast<uint8>((static_cast<float>(level) / triggerLevel) * g_ResetBotChancePercent);
        if (chance > 100)
            chance = 100;
        if (g_DebugMode)
        {
            LOG_INFO("server.loading", "[mod-player-bot-reset] ComputeResetChance: For level {} with trigger {} and scaling, computed chance = {}%.",
                     level, triggerLevel, chance);
        }
    }
    else if (g_DebugMode)
    {
        LOG_INFO("server.loading", "[mod-player-bot-reset] ComputeResetChance: For level {} without scaling, chance = {}%.", level, chance);
    }
    return chance;
}

// -----------------------------------------------------------------------------
// HELPER FUNCTION: Perform the Reset Actions for a Bot Using a Reset Pair
// -----------------------------------------------------------------------------
static void ResetBotWithPair(Player* player, uint8 currentLevel, const ResetPair& pair)
{
    uint8 levelToResetTo = pair.targetLevel;
    // Special handling for Death Knights: if target is less than 55, set to 55.
    if (player->getClass() == CLASS_DEATH_KNIGHT && levelToResetTo < 55)
        levelToResetTo = 55;

    PlayerbotFactory newFactory(player, levelToResetTo);
    newFactory.Randomize(false);

    if (g_DebugMode)
    {
        PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(player);
        std::string playerClassName = botAI ? botAI->GetChatHelper()->FormatClass(player->getClass()) : "Unknown";
        LOG_INFO("server.loading", "[mod-player-bot-reset] ResetBotWithPair: Bot '{}' - {} at level {} was reset to level {} using reset pair (trigger: {}, target: {}).",
                 player->GetName(), playerClassName, currentLevel, levelToResetTo, pair.triggerLevel, pair.targetLevel);
    }

    ChatHandler(player->GetSession()).SendSysMessage("[mod-player-bot-reset] Your level has been reset.");
}

// -----------------------------------------------------------------------------
// PLAYER SCRIPT: OnLogin and OnLevelChanged
// -----------------------------------------------------------------------------
class ResetBotLevelPlayerScript : public PlayerScript
{
public:
    ResetBotLevelPlayerScript() : PlayerScript("ResetBotLevelPlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        if (!player)
            return;
        ChatHandler(player->GetSession()).SendSysMessage("The [mod-player-bot-reset] module is active on this server.");
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!player)
        {
            LOG_ERROR("server.loading", "[mod-player-bot-reset] OnLevelChanged called with nullptr player.");
            return;
        }

        uint8 newLevel = player->GetLevel();
        if (newLevel == 1)
            return;

        // Special case for Death Knights: if level is 55 and class is DK, do not reset.
        if (newLevel == 55 && player->getClass() == CLASS_DEATH_KNIGHT)
            return;

        if (!IsPlayerBot(player))
        {
            if (g_DebugMode)
                LOG_INFO("server.loading", "[mod-player-bot-reset] OnLevelChanged: Player '{}' is not a bot. Skipping reset check.", player->GetName());
            return;
        }

        if (!IsPlayerRandomBot(player))
        {
            if (g_DebugMode)
                LOG_INFO("server.loading", "[mod-player-bot-reset] OnLevelChanged: Player '{}' is not a random bot. Skipping reset check.", player->GetName());
            return;
        }

        // Check each reset pair in descending order; apply the first applicable pair.
        for (const ResetPair &pair : g_ResetPairs)
        {
            if (newLevel >= pair.triggerLevel)
            {
                uint8 resetChance = ComputeResetChance(newLevel, pair.triggerLevel);
                if (g_DebugMode)
                {
                    LOG_INFO("server.loading", "[mod-player-bot-reset] OnLevelChanged: Bot '{}' at level {} qualifies for reset pair (trigger: {}, target: {}) with reset chance {}%.",
                             player->GetName(), newLevel, pair.triggerLevel, pair.targetLevel, resetChance);
                }
                if (urand(0, 99) < resetChance)
                    ResetBotWithPair(player, newLevel, pair);
                break; // Apply only the highest priority applicable reset pair.
            }
        }
    }
};

// -----------------------------------------------------------------------------
// WORLD SCRIPT: Load Configuration on Startup
// -----------------------------------------------------------------------------
class ResetBotLevelWorldScript : public WorldScript
{
public:
    ResetBotLevelWorldScript() : WorldScript("ResetBotLevelWorldScript") { }

    void OnStartup() override
    {
        LoadPlayerBotResetConfig();
        // The reset pairs and other configuration options have been loaded in LoadPlayerBotResetConfig().
    }
};

// -----------------------------------------------------------------------------
// WORLD SCRIPT: OnUpdate Check for Time-Played Based Reset at Trigger Levels
// This handler runs every g_PlayedTimeCheckFrequency seconds and iterates over players.
// For each bot that is at or above a reset trigger and has accumulated at least g_MinTimePlayed
// seconds at the current level, it applies the same reset chance logic and resets the bot if the check passes.
// -----------------------------------------------------------------------------
class ResetBotLevelTimeCheckWorldScript : public WorldScript
{
public:
    ResetBotLevelTimeCheckWorldScript() : WorldScript("ResetBotLevelTimeCheckWorldScript"), m_timer(0) { }

    void OnUpdate(uint32 diff) override
    {
        if (!g_RestrictResetByPlayedTime)
            return;

        m_timer += diff;
        if (m_timer < g_PlayedTimeCheckFrequency * 1000)
            return;
        m_timer = 0;

        if (g_DebugMode)
        {
            LOG_INFO("server.loading", "[mod-player-bot-reset] OnUpdate: Starting time-based reset check...");
        }

        auto const& allPlayers = ObjectAccessor::GetPlayers();
        for (auto const& itr : allPlayers)
        {
            Player* candidate = itr.second;
            if (!candidate || !candidate->IsInWorld())
                continue;
            if (!IsPlayerBot(candidate) || !IsPlayerRandomBot(candidate))
                continue;

            uint8 currentLevel = candidate->GetLevel();

            // Find the highest reset pair applicable for the current level.
            const ResetPair* applicablePair = nullptr;
            for (const ResetPair &pair : g_ResetPairs)
            {
                if (currentLevel >= pair.triggerLevel)
                {
                    applicablePair = &pair;
                    break;
                }
            }
            if (!applicablePair)
                continue;

            // Check if the bot has played at least the minimum time at this level.
            if (candidate->GetLevelPlayedTime() < g_MinTimePlayed)
            {
                if (g_DebugMode)
                {
                    LOG_INFO("server.loading", "[mod-player-bot-reset] OnUpdate: Bot '{}' at level {} has insufficient played time ({} < {} seconds) for reset pair (trigger: {}, target: {}).",
                             candidate->GetName(), currentLevel, candidate->GetLevelPlayedTime(), g_MinTimePlayed,
                             applicablePair->triggerLevel, applicablePair->targetLevel);
                }
                continue;
            }

            uint8 resetChance = ComputeResetChance(currentLevel, applicablePair->triggerLevel);
            if (g_DebugMode)
            {
                LOG_INFO("server.loading", "[mod-player-bot-reset] OnUpdate: Bot '{}' qualifies for time-based reset with reset pair (trigger: {}, target: {}) at level {} with played time {} seconds and reset chance {}%.",
                        candidate->GetName(), applicablePair->triggerLevel, applicablePair->targetLevel, currentLevel,
                        candidate->GetLevelPlayedTime(), resetChance);
            }
            if (urand(0, 99) < resetChance)
            {
                if (g_DebugMode)
                {
                    LOG_INFO("server.loading", "[mod-player-bot-reset] OnUpdate: Reset chance check passed for bot '{}'. Resetting bot using reset pair (trigger: {}, target: {}).",
                            candidate->GetName(), applicablePair->triggerLevel, applicablePair->targetLevel);
                }
                ResetBotWithPair(candidate, currentLevel, *applicablePair);
            }
        }
    }
private:
    uint32 m_timer;
};

// -----------------------------------------------------------------------------
// ENTRY POINT: Register Scripts
// -----------------------------------------------------------------------------
void Addmod_player_bot_resetScripts()
{
    new ResetBotLevelWorldScript();
    new ResetBotLevelPlayerScript();
    new ResetBotLevelTimeCheckWorldScript();
}
