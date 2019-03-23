// stagedSceneGenerator 0.6 (2018-12-15)
// This BZFlag plugin allows setting up staged scenes with fixed tanks and
// shots for generating screenshots. See README.stagedSceneGenerator.txt

/*
Copyright (c) 2018 Scott Wichser
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the FreeBSD Project.

*/

#include "bzfsAPI.h"
#include "plugin_utils.h"

#include <math.h>
#include <vector>

class stagedSceneGenerator : public bz_Plugin, public bz_CustomSlashCommandHandler
{
public:
    virtual const char* Name ()
    {
        return "Staged Scene Generator";
    }
    virtual void Init ( const char* config );
    virtual void Cleanup();
    virtual void Event ( bz_EventData * eventData );
    virtual bool SlashCommand ( int playerID, bz_ApiString, bz_ApiString, bz_APIStringList*);

private:
    bool readConfig(const char* configFile);
    bz_eTeamType teamFromString(std::string team);

    PluginConfig config;

    struct StagedPlayer
    {
        bz_eTeamType team{eNoTeam};
        bool random{false};
        float pos[3] {0.0f, 0.0f, 0.0f};
        float rot{0.0f};
        std::string flag{""};

        int playerID{-1};

        // Used for GM shots
        std::string sectionName{""};

        double lastDeath{0.0};
    };

    std::vector<StagedPlayer> stagedPlayers;

    struct StagedShot
    {
        bz_eTeamType team{eNoTeam};
        float pos[3] {0.0f, 0.0f, 0.0f};
        float dir[3] {1.0f, 0.0f, 0.0f};
        std::string flag{""};

        // Used for GM shots
        std::string targetPlayerSectionName{""};
        int targetPlayerID{-1};
    };

    std::vector<StagedShot> stagedShots;

    double lastShotsFired = -9999.0;
    double delayBetweenShots = 0.0;
    double spawnDelay = 0.0;

    double shotSpeed = 0.01;

    double laserShotSpeed;
    double thiefShotSpeed;

    enum Modes {
        ModeStatic1,
        ModeStatic2,
        ModeNormal
    };

    Modes mode = ModeStatic1;
};

BZ_PLUGIN(stagedSceneGenerator)

void stagedSceneGenerator::Init ( const char* commandLine )
{
    // Cheap hack to detect if the plugin is being loaded from /loadplugin in-game
    if (bz_getCurrentTime() > 10.0)
    {
        bz_debugMessage(0, "ERROR: stagedSceneGeneration can only be loaded from the command line");
        bz_shutdown();
        return;
    }

    bz_debugMessage(4,"stagedSceneGenerator plugin loaded");

    // A configuration file is required
    if (commandLine == NULL || strlen(commandLine) == 0)
    {
        bz_debugMessage(0, "ERROR: stagedSceneGenerator plugin needs a config file");
        bz_shutdown();
        return;
    }
    else
    {
        // Try to read the configuration file
        if (!readConfig(commandLine))
        {
            bz_debugMessage(0, "ERROR: There was an error reading the provided stagedSceneGenerator config file");
            bz_shutdown();
            return;
        }

        // init events here with Register();
        Register(bz_eGetAutoTeamEvent);
        Register(bz_eGetPlayerSpawnPosEvent);
        Register(bz_ePlayerSpawnEvent);
        Register(bz_ePlayerDieEvent);
        Register(bz_ePlayerUpdateEvent);
        Register(bz_ePlayerPartEvent);
        Register(bz_eTickEvent);

        // Allow bots
        bz_updateBZDBBool("_disableBots", false);

        // Disable those pesky height checks so we can go wild
        bz_updateBZDBBool("_disableHeightChecks", true);

        // Calculate the laser/thief shot speed (which uses the normal values of _shotSpeed and _laserAdLife before
        // we mess with them below). Because we set the
        laserShotSpeed = bz_getBZDBDouble("_shotSpeed") * bz_getBZDBDouble("_laserAdLife");
        thiefShotSpeed = bz_getBZDBDouble("_shotSpeed") * bz_getBZDBDouble("_thiefAdLife");

        // Mode Static1 - Very low gravity and tanks spawn slightly in the air.
        // Tank speed is still normal, so it's possible to move as observer
        if (mode == ModeStatic1)
        {
            bz_updateBZDBDouble("_gravity", -0.000001);

            // Because flags dropping in are affected by gravity, set the altitude to 0 so they land immediately
            bz_updateBZDBDouble("_flagAltitude", 0.0);
        }

        // Mode Static2 - Very low tank speed and turning velocity. Normal gravity. Can't drive as observer and must
        // use /roampos to move around. But, tanks explode in the normal arc.
        else if (mode == ModeStatic2)
        {
            bz_updateBZDBDouble("_tankSpeed", -0.000001);
            bz_updateBZDBDouble("_tankAngVel", -0.000001);
        }

        // If this isn't normal mode, set some other stuff.
        if (mode != ModeNormal) {
            // Set some values that affect tanks and shots
            bz_updateBZDBDouble("_shotSpeed", shotSpeed);
            bz_updateBZDBDouble("_shotRange", 0.05);
            bz_updateBZDBDouble("_laserAdLife", 1.0);
            bz_updateBZDBDouble("_thiefAdLife", 1.0);

            // Hard-code this to the normal 3.5 seconds or else beam weapons aren't the correct length.
            bz_updateBZDBDouble("_reloadTime", 3.5);
        }

        // Gotta go fast! (only useful when there are no players and only shots)
        if (stagedPlayers.size() == 0)
            MaxWaitTime = 0.05f;

        // Register a custom command
        bz_registerCustomSlashCommand("scene", this);
    }
}

void stagedSceneGenerator::Cleanup()
{
    // Remove custom command
    bz_removeCustomSlashCommand("scene");

    // Remove event handlers
    Flush();
}

bool stagedSceneGenerator::readConfig(const char* configFile)
{
    // Parse the configuration
    config.read(configFile);

    // If we had any errors, bail out
    if (config.errors > 0)
        return false;

    // Loop through each section of the configuration. There will be one section per tank or shot.
    for (auto section : config.getSections())
    {
        // The main section will contain global settings that affect the plugin.
        if (section == "main")
        {
            for (auto item : config.getSectionItems(section))
            {
                std::string name = makelower(item.first.c_str());
                if (name == "shotdelay") {
                    delayBetweenShots = atof(item.second.c_str());
                    if (delayBetweenShots < 0.0 || delayBetweenShots > 60.0) {
                        bz_debugMessage(0,"ERROR: ShotDelay must be between 0.0 and 60.0 (inclusive)");
                        return false;
                    }
                }
                else if (name == "shotspeed") {
                    shotSpeed = atof(item.second.c_str());
                    if (shotSpeed < 0.01 && shotSpeed > 1000.0) {
                        bz_debugMessage(0,"ERROR: ShotSpeed must be between 0.01 and 1000.0 (inclusive)");
                        return false;
                    }
                }
                else if (name == "spawndelay") {
                    spawnDelay = atof(item.second.c_str());
                    if (spawnDelay < 0.0 || spawnDelay > 60.0) {
                        bz_debugMessage(0,"ERROR: SpawnDelay must be between 0.0 and 60.0 (inclusive)");
                        return false;
                    }
                }
                else if (name == "mode")
                {
                    std::string _mode = makelower(item.second.c_str());
                    if (_mode == "static1")
                        mode = ModeStatic1;
                    else if (_mode == "static2")
                        mode = ModeStatic2;
                    else if (_mode == "normal")
                        mode = ModeNormal;
                    else {
                        bz_debugMessage(0,"ERROR: Mode must be one of: static1, static2, or normal");
                        return false;
                    }
                }
            }
            continue;
        }

        // Store a lowercase copy of the type for comparisons
        std::string type = makelower(config.item(section, "type").c_str());

        // Congratulations Mr. and Mrs. Abrams, it's a tank
        if (type == "tank")
        {
            StagedPlayer p;

            p.sectionName = makelower(section.c_str());
            p.team = teamFromString(config.item(section, "team"));
            p.flag = makeupper(config.item(section, "flag").c_str());

            // A tank can be randomly spawned or set to spawn at a fixed location
            p.random = (makelower(config.item(section, "random").c_str()) == "true");

            // If it's not random, read the position and rotation, if set
            if (!p.random)
            {
                std::string pos = config.item(section, "pos");
                if (pos.size() > 0)
                {
                    std::vector<std::string> pos2 = tokenize(pos, std::string(" "), 3, false);
                    if (pos2.size() == 3)
                    {
                        p.pos[0] = atof(pos2.at(0).c_str());
                        p.pos[1] = atof(pos2.at(1).c_str());
                        p.pos[2] = atof(pos2.at(2).c_str());
                    }
                }

                std::string rot = config.item(section, "rot");
                if (rot.size() > 0)
                    p.rot = atof(rot.c_str());
            }

            // Add this staged player to our list
            stagedPlayers.push_back(p);
        }
        // Guess I'll take a shot at this
        else if (type == "shot")
        {
            StagedShot s;

            s.team = teamFromString(config.item(section, "team"));

            // A flag abbreviation can be provided to change the shot type
            s.flag = makeupper(config.item(section, "flag").c_str());

            // If it's a GM, we can also have a target
            if (s.flag == "GM") {
                s.targetPlayerSectionName = makelower(config.item(section, "target").c_str());
            }

            std::string pos = config.item(section, "pos");
            if (pos.size() > 0)
            {
                std::vector<std::string> pos2 = tokenize(pos, std::string(" "), 3, false);
                if (pos2.size() == 3)
                {
                    s.pos[0] = atof(pos2.at(0).c_str());
                    s.pos[1] = atof(pos2.at(1).c_str());
                    s.pos[2] = atof(pos2.at(2).c_str());
                }
            }

            std::string strRot = config.item(section, "rot");
            std::string strElev = config.item(section, "elev");
            double rot = 0, elev = 0;
            if (strRot.size() > 0)
                rot = atof(strRot.c_str()) * M_PI / 180.0;

            if (strElev.size() > 0)
                elev = (atof(strElev.c_str()) * M_PI / 180.0);
            elev += M_PI / 2;

            s.dir[0] = sin(elev) * cos(rot);
            s.dir[1] = sin(elev) * sin(rot);
            s.dir[2] = -cos(elev);

            // Add this staged shot to our list
            stagedShots.push_back(s);
        }

    }

    return true;
}

bz_eTeamType stagedSceneGenerator::teamFromString(std::string team)
{
    team = makelower(team.c_str());
    if (team == "red") return eRedTeam;
    else if (team == "green") return eGreenTeam;
    else if (team == "blue") return eBlueTeam;
    else if (team == "purple") return ePurpleTeam;
    else if (team == "hunter") return eHunterTeam;
    else if (team == "rabbit") return eRabbitTeam;

    return eRogueTeam;
}

void stagedSceneGenerator::Event(bz_EventData *eventData)
{
    switch(eventData->eventType)
    {
    case bz_eGetAutoTeamEvent:
    {
        bz_GetAutoTeamEventData_V1* data = (bz_GetAutoTeamEventData_V1*)eventData;
        // Ignore clients joining as observer
        if (data->team != eObservers)
        {
            bz_debugMessagef(0, "INFO: Requested team for %d is %d", data->playerID, data->team);
            // Find the first staged player that does not have an associated player
            for (auto &stagedPlayer : stagedPlayers)
            {
                // We found an available staged player
                if (stagedPlayer.playerID == -1)
                {
                    bz_debugMessagef(0, "INFO: Found available staged player for %d", data->playerID);
                    stagedPlayer.playerID = data->playerID;
                    data->team = stagedPlayer.team;
                    data->handled = true;

                    for (auto &stagedShot : stagedShots) {
                        if (stagedShot.targetPlayerSectionName == stagedPlayer.sectionName)
                            stagedShot.targetPlayerID = stagedPlayer.playerID;
                    }

                    break;
                }
            }

            // If all the staged players are ... staged ... then just make this player an observer.
            // This has the positive side effect of also making extra -solo bots go away.
            if (!data->handled)
            {
                bz_debugMessagef(0, "WARNING: No available staged player for %d, so assigning observer", data->playerID);
                data->team = eObservers;
                data->handled = true;
            }
        }
        break;
    }

    case bz_eGetPlayerSpawnPosEvent:
    {
        bz_GetPlayerSpawnPosEventData_V1* data = (bz_GetPlayerSpawnPosEventData_V1*)eventData;

        // See if this is a staged player
        for (auto stagedPlayer : stagedPlayers)
        {
            // We found a match!
            if (stagedPlayer.playerID == data->playerID)
            {
                bz_debugMessagef(0, "INFO: Spawning staged player %d", data->playerID);

                // If this isn't a random spawn, set the position and rotation
                if (!stagedPlayer.random)
                {
                    data->pos[0] = stagedPlayer.pos[0];
                    data->pos[1] = stagedPlayer.pos[1];
                    data->pos[2] = stagedPlayer.pos[2];
                    data->rot = stagedPlayer.rot * M_PI / 180.0f;
                }

                // If using mode static1, spawn the tank slightly in the air so that it won't be moving around
                if (mode == ModeStatic1)
                    data->pos[2] += 0.01;

                data->handled = true;

                // Break out of the 'for' loop
                break;
            }
        }
        break;
    }

    case bz_ePlayerSpawnEvent:
    {
        if (spawnDelay == 0.0)
            break;

        bz_PlayerSpawnEventData_V1* data = (bz_PlayerSpawnEventData_V1*)eventData;

        for (auto &stagedPlayer : stagedPlayers)
        {
            if (stagedPlayer.playerID == data->playerID && !stagedPlayer.flag.empty())
            {
                bz_debugMessagef(0, "INFO: Giving staged player %d the %s flag", data->playerID, stagedPlayer.flag.c_str());

                bz_givePlayerFlag(data->playerID, stagedPlayer.flag.c_str(), false);

                break;
            }
        }

        break;
    }

    case bz_ePlayerDieEvent:
    {
        bz_PlayerDieEventData_V2* data = (bz_PlayerDieEventData_V2*)eventData;

        for (auto &stagedPlayer: stagedPlayers)
        {
            if (stagedPlayer.playerID == data->playerID) {
                // Disable spawning so we can add a delay between the explosion ending and the respawn
                bz_setPlayerSpawnable(data->playerID, false);
                stagedPlayer.lastDeath = data->eventTime;
                break;
            }
        }
    }

    case bz_ePlayerUpdateEvent:
    {
        // We don't need to bother with this in normal mode
        if (mode == ModeNormal)
            break;

        bz_PlayerUpdateEventData_V1* data = (bz_PlayerUpdateEventData_V1*)eventData;

        if (mode == ModeStatic1) {
            // We spawn tanks in the air, and have gravity set real low. Eventually a tank might land and start
            // moving, so kill 'em if they do.
            if (data->state.status == eAlive && (data->state.velocity[0] != 0.0f || data->state.velocity[1] != 0.0f))
            {
                bz_debugMessagef(0, "NOTE: Killing player '%s' because they moved", bz_getPlayerCallsign(data->playerID));
                bz_killPlayer(data->playerID, false);
            }
        }
        else if (mode == ModeStatic2) {
            // Find the staged player record
            for (auto &stagedPlayer : stagedPlayers)
            {
                if (stagedPlayer.playerID == data->playerID) {
                    // If they have moved a bit from their staged position, kill 'em
                    if (fabs(stagedPlayer.pos[0] - data->state.pos[0]) > 0.1 || fabs(stagedPlayer.pos[1] - data->state.pos[1]) > 0.1) {
                        bz_debugMessagef(0, "NOTE: Killing player '%s' because they moved", bz_getPlayerCallsign(data->playerID));
                        bz_killPlayer(data->playerID, false);
                    }
                    break;
                }
            }
        }
        break;
    }

    case bz_ePlayerPartEvent:
    {
        bz_PlayerJoinPartEventData_V1* data = (bz_PlayerJoinPartEventData_V1*)eventData;

        // When a player leaves, see if they were assigned to a staged player and release them
        for (auto &stagedPlayer : stagedPlayers)
        {
            if (stagedPlayer.playerID == data->playerID) {
                stagedPlayer.playerID = -1;

                for (auto &stagedShot : stagedShots) {
                    if (stagedShot.targetPlayerSectionName == stagedPlayer.sectionName)
                        stagedShot.targetPlayerID = -1;
                }
            }
        }

        break;
    }

    case bz_eTickEvent:
    {
        bz_TickEventData_V1* data = (bz_TickEventData_V1*)eventData;

        // I'ma firing my BLAAAAARRRR
        if (data->eventTime > lastShotsFired + bz_getBZDBDouble("_reloadTime") + delayBetweenShots)
        {
            lastShotsFired = data->eventTime;
            for (auto stagedShot : stagedShots)
            {
                if (mode != ModeNormal) {
                    // The laser and thief length is based on shot speed, so change the shot speed for this shot
                    if (stagedShot.flag == "L")
                        bz_updateBZDBDouble("_shotSpeed", laserShotSpeed);
                    else if (stagedShot.flag == "TH")
                        bz_updateBZDBDouble("_shotSpeed", thiefShotSpeed);
                }

                // FIRE!!!
                bz_fireServerShot(stagedShot.flag.c_str(), stagedShot.pos, stagedShot.dir, stagedShot.team, stagedShot.targetPlayerID);
                bz_debugMessagef(1, "Firing shot at %f %f %f", stagedShot.pos[0], stagedShot.pos[1], stagedShot.pos[2]);

                if (mode != ModeNormal) {
                    // If we just shot a laser or thief, remember to set the shot speed again
                    if (stagedShot.flag == "L" || stagedShot.flag == "TH")
                        bz_updateBZDBDouble("_shotSpeed", shotSpeed);
                }
            }
        }

        if (spawnDelay > 0.0)
        {
            double explodeTime = bz_getBZDBDouble("_explodeTime");

            for (auto &stagedPlayer : stagedPlayers)
            {
                if (data->eventTime > stagedPlayer.lastDeath + explodeTime + spawnDelay)
                    bz_setPlayerSpawnable(stagedPlayer.playerID, true);
            }
        }
    }

    default:
        break;
    }
}

bool stagedSceneGenerator::SlashCommand ( int playerID, bz_ApiString /*cmd*/, bz_ApiString, bz_APIStringList* cmdParams )
{
    std::string subcommand = makelower(cmdParams->get(0).c_str());

    if (subcommand == "reset") {
        for (auto &stagedPlayer : stagedPlayers)
        {
            if (stagedPlayer.playerID > -1)
                bz_killPlayer(stagedPlayer.playerID, false);
        }
    }
    else {
        bz_sendTextMessage(BZ_SERVER, playerID, "Usage: /scene reset");
    }

    return true;
}

// Local Variables: ***
// mode: C++ ***
// tab-width: 4 ***
// c-basic-offset: 4 ***
// indent-tabs-mode: nil ***
// End: ***
// ex: shiftwidth=4 tabstop=4
