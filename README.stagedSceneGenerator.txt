BZFlag Server Plugin: stagedSceneGenerator
================================================================================

This BZFlag plugin allows setting up staged scenes with fixed tanks and shots
for generating screenshots.

LIMITATIONS:
 - Super bullet in experimental mode will not look correct as the geoshot's
   length is based on shot speed.
 - Guided missiles may also have visual differences as the trails may be based
   on the shot speed.
 - Since shots are generated on the server the shot limit is 255.
 - Presently, flags are not attached to a tank. So even though you can make a
   tank appear to shoot another shot type (laser, SW, SB), the staged tank will
   not be holding a flag.
 - Can not simulate a zoned tank as the API does not allow toggling this.
 - Simulating a shot that has ricocheted will require manual pre-computing and
   will not show the rico effect on the wall. For laser this isn't a problem as
   you can fire a laser from the normal muzzle position and it will ricochet as
   normal.
 - Because of the suggestion to use -noTeamKills, it is possible that otherwise
   lethal laser shots (like a ricocheting laser or a shockwave) will not kill.

Loading the plugin
--------------------------------------------------------------------------------

This plugin requires a configuration file (see stagedSceneGenerator.cfg for an
example of the format) as well as a couple other bzfs options. Since we may be
generating server-side shots near other tanks (such as to simulate a tank
shooting a laser) we want to disallow teamkilling. To prevent AI tanks from
shooting on their own, we also want to disallow shooting.

So pass these options to bzfs, either via the command line or the bzfs config:
  -noTeamKills -ms 0

Then to load the plugin, specify a configuration file using this format:
  -loadplugin stagedSceneGenerator,<configfilename>
For example:
  -loadplugin stagedSceneGenerator,/path/to/my/stagedSceneGenerator.cfg


Running the game client
--------------------------------------------------------------------------------
To populate the staged tanks, you may use solo bots from the game client. Start
the game client from the command line, adjusting the 5 to reflect how many
staged tanks you need to create:
  bzflag -solo 5

Then join your server. To turn off most of the user interface, bring up the
chat prompt in the game with the 'n' key and type:
  /localset noGUI 1
You can then also turn off the radar and chat console with F1 and F2.


Configuration File
--------------------------------------------------------------------------------

The configuration file uses an INI-like format. Most options are optional,
though you'll generally want at least a team, position, and rotation. Note the
language below with MUST and MAY, with MUST meaning required and MAY being
optional.

Each staged tank or shot MUST start with a unique section name contained in
square brackets:
    [SomeName]

Then a type MUST be specified, either tank or shot.
    type = tank
  or
    type = shot

For both types, a team, position (pos) or rotation (rot) MAY be provided. The
team takes a lowercase string of either red, green, blue, purple, hunter,
rabbit, or rogue. The position takes three floating point numbers,
corresponding to the x, y, and z coordinates in the game, with z being height.
The rotation is represented in degree.
    team = purple
    pos = 10 20 30
    rot = 180

For tanks, there is the option to give a flag so it'll appear to be carrying a
flag. This option will not have any affect on shots as those are defined
separately.
    flag = US

For tanks, there is the option of using random spawns instead of specifying
the position and rotation. By default this is turned off, but to enable it:
    random = true

For shots, there are two additional options. You MAY specify the flag type of
the shot, so it is possible to use other shot types. Review the limitations
section at the top of the file for some caveats. You MAY also set the elevation
angle of a shot, with 0 being level with the ground and 90 being straight up.
    flag = L
    elev = 45
