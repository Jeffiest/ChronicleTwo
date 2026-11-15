#!/usr/bin/env python3
"""Live co-op test for Chronicle Two: two real games on the disc's data, through a local relay.

Starts the co-op relay on a free port, then a host game and a guest game, headless at the real tick rate
(DC2_HEADLESS_BOUNDED=1), each with its own throwaway save folder holding MoonMod's co-op mod and its test driver
(MoonMod tools/tests/coop/live/coopdrive, which plays the scenario's script). The co-op mod's live-test hook (storage
"autotest") makes the host open a room and the guest join it by the code the host's log gives. Both games log what
they see once a second ("COOPTEST state", "DRIVE state"); the scenario's checks read those logs.

    python3 -I tools/coop/live.py --scenario meet --keep --out /tmp/coop-meet

Needs extracted retail data (--data, DC2_DATA, or the gauntlet's cache) and MoonMod's checkout (--moonmod). With
neither it prints NOT RUN and exits 2: a scenario is never reported as passed without the game's data.

Exit 0 when every check passes, 1 when one fails, 2 when the run could not start.
"""

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOME = Path.home()

SCENARIOS = {}
CHECKS = {}


def scenario(name):
    def add(fn):
        SCENARIOS[name] = fn
        return fn
    return add


def checks(name):
    def add(fn):
        CHECKS[name] = fn
        return fn
    return add


# --- the guest's way to the co-op page ----------------------------------------------------------------------------
# From --jump title: Start past the title to the menu, then down to the last row. The menu stops at its last row and
# the co-op mod's is the only mod row these saves register, so more presses than rows land on it. Start opens it:
# the menu takes Start as well as its confirm button, and the page that opens does nothing with Start.
GUEST_TITLE_INPUT = """\
300 start
306
420 start
426
520 down
526
560 down
566
600 down
606
640 down
646
680 down
686
720 down
726
800 start
806
"""


# The joiner's own save, from --jump dungeon:0:1 (the first dungeon's floor list, story progress 2): Triangle opens
# Save Progress, Cross picks File 1, and the game asks "OK to save?" with No under the cursor: Left, Cross. The
# presses are spaced for a menu that takes its time to open; a second Left and Cross answer the question when the
# first pair came too early (they picked the file instead), and do no harm when the save is already written.
OWN_SAVE_INPUT = """300 triangle
306
520 cross
526
820 cross
826
1120 left
1126
1150 cross
1156
1500 left
1506
1560 cross
1566
"""


@scenario('meet')
def meet():
    """The guest joins the host standing in Palm Brinks; each game shows the other's hero walking there."""
    host = [
        # a save's text of a quarter of a megabyte goes through the operations that carry one, in and back out
        {'when': 'town', 'after': 300, 'op': 'big_text', 'tag': 'DCP2!', 'size': 240000},
        {'after': 3300, 'op': 'mark', 'text': 'town together'},
        {'after': 600, 'op': 'mark', 'text': 'end'},
    ]
    guest = [{'when': 'visit', 'op': 'mark', 'text': 'arrived'}]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 7200, 'guest_frames': 6600}


def floor_together(dungeon, floor_id):
    host = [
        {'when': 'town', 'after': 1800, 'op': 'enter_floor', 'dungeon_id': dungeon, 'floor_id': floor_id},
        {'when': 'playing', 'after': 1500, 'op': 'mark', 'text': 'both on the floor'},
        {'when': 'monster', 'after': 30, 'op': 'strike', 'amount': 1000000},
        {'when': 'monster', 'after': 240, 'op': 'strike', 'amount': 1000000},
        {'after': 900, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'dungeon', 'after': 1200, 'op': 'mark', 'text': 'on the floor'},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800}


@scenario('floor')
def floor():
    """The guest joins in Palm Brinks, the host walks into the Underground Channel (floor 2, a random layout) and the
    guest follows by its offer; both stand on the same floor, the guest's monsters are copies of the host's, and one
    the host strikes down is gone from the guest's floor too."""
    return floor_together(0, 2)


@scenario('floor_first')
def floor_first():
    """The other way round: the joiner goes into the Underground Channel alone and strikes a monster down, the host
    is offered to join and follows. The floor is the joiner's to run (it was there first): the host's monsters copy
    the joiner's as they stand, and the joiner's stay its own."""
    host = [
        {'when': 'playing', 'after': 1500, 'op': 'mark', 'text': 'both on the floor'},
        {'after': 600, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'town', 'after': 900, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 2},
        {'when': 'monster', 'after': 120, 'op': 'strike', 'amount': 1000000},
        {'when': 'playing', 'after': 2400, 'op': 'mark', 'text': 'on the floor'},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800,
            'host_autotest': {'follow': True}}


@scenario('clear')
def clear():
    """A floor cleared for both: the two stand on a floor, the host strikes every monster down (from where it stands:
    most of them out of its sight), the joiner's floor is empty too, and each game gives the floor's own clear (no
    monster in the game's count, no mimic chest, system script 1500), with the joiner still in the visit there."""
    host = [
        {'when': 'town', 'after': 1800, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 2},
        {'when': 'playing', 'after': 1500, 'op': 'strike_all'},
        {'after': 300, 'op': 'strike_all'},
        {'after': 1500, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'dungeon', 'after': 1200, 'op': 'mark', 'text': 'on the floor'},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800}


@scenario('hurt')
def hurt():
    """Both stand on a floor and its monsters come for them: a blow that lands on the joiner's hero in the host's game
    (which runs the monsters) costs the joiner life in their own. The host's own hero takes no hits here, so the
    floor lasts."""
    host = [
        {'when': 'town', 'after': 1800, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 2},
        # the floor's own monsters may never come to the way in: two foes of the test's own beside the heroes
        {'when': 'playing', 'after': 1500, 'op': 'spawn_foe', 'dx': 40},
        {'after': 600, 'op': 'spawn_foe', 'dx': -40},
        {'after': 2400, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'dungeon', 'after': 1200, 'op': 'mark', 'text': 'on the floor'},
    ]
    # A floor whose monsters are known to come to the way in (seen in a run of 2026-11-14): both games build it from
    # this seed instead of the room's.
    seed = {'seed': 288166145}
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800,
            'host_extra': ['--invincible'], 'host_autotest': seed, 'guest_autotest': seed}


@scenario('down')
def down():
    """Down, not out: a foe that hits far harder than the joiner's hero can take strikes them down on the floor. The
    joiner lies there instead of the game's own Game Over, the host beside them holds Revive, and the joiner is
    back on their feet."""
    host = [
        {'when': 'town', 'after': 1800, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 2},
        {'when': 'playing', 'after': 1500, 'op': 'spawn_foe', 'dx': 40, 'attack': 60},
        {'after': 3000, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'dungeon', 'after': 1200, 'op': 'mark', 'text': 'on the floor'},
    ]
    seed = {'seed': 288166145}
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800,
            'host_extra': ['--invincible'], 'host_autotest': dict(seed, hold_revive=True), 'guest_autotest': seed}


# A walk for the pictures: the left stick pushed forward for a quarter of a second, so the two heroes stand a few
# steps apart.
WALK_AT = """%d 128 0 128 128
%d
"""


@scenario('look')
def look():
    """For the eye, not for checks: both on a floor, the joiner walks a few steps from the way in, and the host's game
    ends first, so its last frame (--shots) shows the joiner's hero standing apart, in what that player wears."""
    sc = floor_together(0, 2)
    sc.update({'host_frames': 6000, 'guest_frames': 7800, 'guest_walk': 4200})
    return sc


@scenario('look_town')
def look_town():
    """For the eye: the two in Palm Brinks. A game started with --jump town:10 stands the host at the map's origin,
    and the joiner comes in at the map's entrance, some 570 units away, and walks down on the stick. The host's game
    ends first. The way the stick walks depends on the camera the visit starts with, which is not the same from run
    to run, so the joiner is not always in the host's last frame."""
    host = [{'when': 'town', 'after': 300, 'op': 'mark', 'text': 'in town'}]
    guest = [{'when': 'visit', 'op': 'mark', 'text': 'arrived'}]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 6000, 'guest_frames': 7800,
            'guest_moves': [(3600, 128, 255, 125)]}


@checks('look_town')
def check_look_town(host, guest, result):
    c = Checks(host, guest, result)
    c.visit()
    c.together('T', 'in town')
    return c.list


@scenario('look_guest')
def look_guest():
    """The same from the other side: the host walks, and the joiner's game ends first."""
    sc = floor_together(0, 2)
    sc.update({'host_frames': 9000, 'guest_frames': 5400, 'host_walk': 4800})
    return sc


# A button pressed for six frames (an input script's line: the frame, the button; then the frame it is let go). A
# chord "a+b+c" holds a and b first and adds c three frames later, so c's press comes while the others are held.
PRESS_AT = """%d %s
%d
"""


def press_lines(presses):
    lines = ''
    for frame, button in presses:
        *held, last = button.split('+')
        if held:
            lines += '%d %s\n' % (frame, ' '.join(held))
            frame += 3
        lines += PRESS_AT % (frame, ' '.join(held + [last]), frame + 6)
    return lines

# The ridepod as the story gives it: its first four parts, then the Core, which fits them and brings the ridepod into
# the party (CUserDataManager::GetItemNotOver); --give gives them on arrival, in this order.
RIDEPOD_GIFTS = '%d,%d,%d,%d,%d' % (0x87, 0x91, 0x9B, 0xA5, 0xF6)


@scenario('ridepod')
def ridepod():
    """For the eye and the logs: both on a floor, the host (Max) climbs into the ridepod (R3, the game's own change)
    and walks a few steps, and the joiner's game ends first, so its last frame shows the host's ridepod as itself."""
    sc = floor_together(0, 2)
    # (the Core brings an empty ridepod, which cannot be climbed into: it is filled first, in town)
    sc['host'].insert(0, {'when': 'town', 'after': 120, 'op': 'ridepod_fuel', 'amount': 300})
    sc['host'].insert(2, {'when': 'playing', 'after': 2700, 'op': 'player_log'})
    # Neither player falls (while the joiner is down, R3 is also the host's Revive). A game started with --jump
    # starts in the debug mode, where R3 on a floor opens the debug menu instead of the change: the second pad's
    # Circle, the main loop's own switch, turns the debug mode off first (on one pad, L2 and Select held stand for
    # the second pad).
    sc.update({'host_frames': 9000, 'guest_frames': 5700, 'host_presses': [(4100, 'l2+select+circle'), (4200, 'r3')],
               'host_walk': 4900,
               'host_extra': ['--invincible', '--give', RIDEPOD_GIFTS], 'guest_extra': ['--invincible']})
    return sc


@checks('ridepod')
def check_ridepod(host, guest, result):
    c = Checks(host, guest, result)
    c.visit()
    c.together('D', 'on the floor')
    rode = [s for s in host['drive'] if s.get('character') == 'ridepod']
    c.check(rode, 'the host rides the ridepod', [s.get('character') for s in host['drive']][-4:])
    drawn = [l for l in guest['lines'] if 'is drawn as the ridepod' in l]
    failed = [l for l in guest['lines'] if 'ridepod did not load' in l]
    c.check(drawn and not failed, "the joiner's game draws the ridepod itself, not the hero standing in", (drawn + failed)[:3])
    return c.list


@scenario('preset')
def preset():
    """The same on a floor of Starlight Canyon, whose floors are whole preset layouts picked by the game's generator:
    the room's seed has to decide the pick as it decides a random layout."""
    return floor_together(2, 1)


@scenario('own')
def own():
    """A joiner brings a save of their own: its things come along, what changes in the visit is written back into it,
    its story stays as it was, and the host keeps nothing of theirs."""
    host = [
        {'when': 'town', 'after': 4200, 'op': 'mark', 'text': 'town together'},
        {'after': 300, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'town', 'after': 300, 'op': 'gilda', 'amount': 4321},
        {'after': 1500, 'op': 'mark', 'text': 'kept'},  # the things are written every 15 seconds here (keep_every)
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 5400, 'guest_frames': 4800,
            'guest_own': True, 'guest_settings': {'keep_every': 15}}


@scenario('gate')
def gate():
    """A joiner with no save past the talk with Donny: the page says so and no visit starts."""
    host = [{'when': 'town', 'after': 2400, 'op': 'mark', 'text': 'end'}]
    guest = [{'when': 'visit', 'op': 'mark', 'text': 'arrived'}]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 3000, 'guest_frames': 2400, 'guest_own': False}


@scenario('heroes')
def heroes():
    """One hero each: the host plays Max and the joiner Monica; the joiner asks to swap, the host says yes, and they
    trade."""
    host = [
        {'when': 'town', 'after': 4500, 'op': 'mark', 'text': 'town together'},
        {'after': 300, 'op': 'mark', 'text': 'end'},
    ]
    guest = [{'when': 'visit', 'op': 'mark', 'text': 'arrived'}]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 6000, 'guest_frames': 5400,
            'host_autotest': {'swap_answer': True}, 'guest_autotest': {'swap_after': 900}}


@scenario('flags')
def flags():
    """The world as it changes: a story flag the joiner sets reaches the host's world, and one the host sets reaches
    the joiner's, while both walk Palm Brinks. (Flags 1500 and 1501: far past the ones the story uses.)"""
    host = [
        {'when': 'town', 'after': 2400, 'op': 'flag_set', 'id': 1501},
        {'after': 3000, 'op': 'flag_log', 'id': 1500},
        {'after': 30, 'op': 'flag_log', 'id': 1501},
        {'after': 300, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'town', 'after': 600, 'op': 'flag_set', 'id': 1500},
        # (the two games do not run at one pace: the joiner looks late, well after the host's flag)
        {'after': 4200, 'op': 'flag_log', 'id': 1501},
        {'after': 30, 'op': 'flag_log', 'id': 1500},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 7200, 'guest_frames': 6600}


@scenario('towns')
def towns():
    """Georama for both: a change to a town in the joiner's game (an analysis condition met in Balance Valley, town 1)
    reaches the host's world, and one in the host's (Veniccio, town 2) reaches the joiner's, while both walk Palm
    Brinks. Each game logs a number for each town before and after."""
    host = [
        {'when': 'town', 'after': 300, 'op': 'town_log', 'town': 1},
        {'after': 10, 'op': 'town_log', 'town': 2},
        {'after': 2100, 'op': 'condition_set', 'town': 2, 'id': 41},
        {'after': 3000, 'op': 'town_log', 'town': 1},
        {'after': 10, 'op': 'town_log', 'town': 2},
        {'after': 300, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'town', 'after': 600, 'op': 'condition_set', 'town': 1, 'id': 40},
        # (the two games do not run at one pace: the joiner looks late, well after the host's change)
        {'after': 4200, 'op': 'town_log', 'town': 1},
        {'after': 10, 'op': 'town_log', 'town': 2},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 7200, 'guest_frames': 6600}


@scenario('build')
def build():
    """Georama seen by both: the two stand in Sindain, the joiner puts up four houses around them (the test's own
    part, made from Firbits' House, placed through the game's placement rules), and the host, who never left the
    spot, has them standing on its screen: no walk back to the town's entrance."""
    house, block = {'template': 47, 'town': 0}, {'template': 36, 'town': 0}
    host = [
        # (both games know the test's parts: a step that names a template registers it, here as there)
        dict({'when': 'town', 'after': 600, 'op': 'georama_probe'}, **house),
        dict({'after': 10, 'op': 'mark', 'text': 'knows the block'}, **block),
        {'after': 4200, 'op': 'georama_probe', 'town': 0},
        {'after': 300, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        # spots the town is known to take (place_probe), beyond the host's hero from the host's camera
        dict({'when': 'town', 'after': 900, 'op': 'part_place', 'spots': [[-300, -320], [300, -320]]}, **house),
        dict({'after': 120, 'op': 'part_place', 'spots': [[0, -440], [-200, -120], [200, -120]]}, **block),
        {'after': 600, 'op': 'georama_probe', 'town': 0},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:0', 'host_frames': 7200, 'guest_frames': 6600}


# The host's walk in Sindain from where it stands (0,0,0): the left stick held forward and to the right from frame
# 900 for 64 frames, which (walk_probe, 2026-11-15; the camera turns as the hero goes) leaves the hero at 390,0,28.
WALK_RIGHT = """%d 255 0 128 128
%d
"""
SHOVE_WALK = (900, 900 + 64)
SHOVE_SPOT = (390, 28)
# The nearest spot to it that the town takes the test's house at (house_probe, 2026-11-15): 40 units off, so the house
# stands over the hero.
SHOVE_HOUSE = (350, 28)


@scenario('walk_probe')
def walk_probe():
    """For writing scenarios: one game alone in Sindain walks forward and to the right, and logs where it stands each
    second (DRIVE state pos=)."""
    host = [{'when': 'town', 'after': 1500, 'op': 'mark', 'text': 'end'}]
    return {'host': host, 'guest': [], 'host_jump': 'town:0', 'host_frames': 2100, 'guest_frames': 0, 'solo': True,
            'host_input': WALK_RIGHT % SHOVE_WALK}


@scenario('shove')
def shove():
    """A part put up where a player stands: the host walks to a spot in Sindain (SHOVE_SPOT), the
    joiner puts one up there, and in the host's game, rebuilt where it stands, the host is set down beside the house
    on ground it can walk (not inside the house), and walks on."""
    house = {'template': 47, 'town': 0}
    host = [
        dict({'when': 'town', 'after': 600, 'op': 'georama_probe'}, **house),
        {'after': 4800, 'op': 'georama_probe', 'town': 0},
        {'after': 300, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        dict({'when': 'town', 'after': 900, 'op': 'part_place', 'spots': [list(SHOVE_HOUSE)]}, **house),
        {'after': 600, 'op': 'georama_probe', 'town': 0},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:0', 'host_frames': 7200, 'guest_frames': 6600,
            'host_input': WALK_RIGHT % SHOVE_WALK}


@scenario('house_probe')
def house_probe():
    """For writing scenarios: one game alone in Sindain tries the test's house at spots nearest SHOVE_SPOT first, and
    logs which the placement rules take (the first one logged is the nearest that takes it)."""
    spots = sorted(([SHOVE_SPOT[0] + dx, SHOVE_SPOT[1] + dz] for dx in range(-200, 201, 40) for dz in range(-200, 201, 40)),
                   key=lambda s: (s[0] - SHOVE_SPOT[0]) ** 2 + (s[1] - SHOVE_SPOT[1]) ** 2)
    host = [{'when': 'town', 'after': 600, 'op': 'part_place', 'template': 47, 'town': 0, 'stock': 50, 'spots': spots},
            {'after': 60, 'op': 'mark', 'text': 'end'}]
    return {'host': host, 'guest': [], 'host_jump': 'town:0', 'host_frames': 1200, 'guest_frames': 0, 'solo': True}


@scenario('place_probe')
def place_probe():
    """For writing scenarios: one game alone in Sindain tries to put down parts made from several of the town's own
    (a house, a block, a tree, a log), and logs which the placement rules take and where."""
    host = [{'when': 'town', 'after': 600, 'op': 'georama_probe', 'town': 0, 'list': True}]
    # blocks over a grid on the far side of the hero from the camera, to learn where the town can be built on
    spots = [[x, z] for z in range(-120, -1000, -80) for x in range(-400, 401, 100)]
    host.append({'after': 60, 'op': 'part_place', 'template': 36, 'town': 0, 'stock': 200, 'spots': spots})
    host += [{'after': 60, 'op': 'georama_probe', 'town': 0}, {'after': 60, 'op': 'mark', 'text': 'end'}]
    return {'host': host, 'guest': [], 'host_jump': 'town:0', 'host_frames': 1800, 'guest_frames': 0, 'solo': True}


@scenario('georama_probe')
def georama_probe():
    """For writing scenarios, not a check of co-op: one game alone in Sindain logs what its Georama offers and holds."""
    host = [
        {'when': 'town', 'after': 600, 'op': 'georama_probe', 'town': 0, 'list': True},
        {'after': 60, 'op': 'mark', 'text': 'end'},
    ]
    return {'host': host, 'guest': [], 'host_jump': 'town:0', 'host_frames': 1500, 'guest_frames': 0, 'solo': True}


@scenario('gate_host')
def gate_host():
    """A host whose story has not reached Donny (the game's opening, as Monica): no room opens."""
    host = [{'when': 'dungeon', 'after': 900, 'op': 'mark', 'text': 'end'}]
    return {'host': host, 'guest': [], 'host_jump': 'opening', 'host_where': 'dungeon', 'host_frames': 1500, 'guest_frames': 0,
            'expect_no_room': True}


# --- the logs -------------------------------------------------------------------------------------------------------

STATE_RE = re.compile(r'(\w+)=(\[[^\]]*\]|\S+)')


def parse(log_text):
    out = {'coop': [], 'drive': [], 'lines': log_text.splitlines(), 'events': []}
    for line in out['lines']:
        m = re.search(r'(COOPTEST|DRIVE) (.*)$', line)
        if not m:
            continue
        kind, rest = m.groups()
        if rest.startswith('state '):
            fields = dict(STATE_RE.findall(rest[6:]))
            (out['coop'] if kind == 'COOPTEST' else out['drive']).append(fields)
        else:
            out['events'].append(kind + ' ' + rest)
    return out


def peers_of(state):
    text = state.get('peers', '[]').strip('[]')
    peers = {}
    for item in text.split():
        parts = item.split(':')
        # peer:scene...:seen:age:slot, where a scene holds colons itself (D0:0:seed)
        peers[int(parts[0])] = {'scene': ':'.join(parts[1:-3]), 'seen': parts[-3], 'age': int(parts[-2]), 'slot': parts[-1]}
    return peers


def shown(states, peer, kind):
    """Seconds in which this game stood in a place of `kind` (T town, D floor) and showed `peer` there as an avatar."""
    hits = 0
    for s in states:
        scene = s.get('scene', 'nil')
        if not scene.startswith(kind):
            continue
        p = peers_of(s).get(peer)
        if p and p['scene'] == scene and p['slot'] != '-':
            hits += 1
    return hits


class Checks:
    def __init__(self, host, guest, result):
        self.host, self.guest, self.result, self.list = host, guest, result, []

    def check(self, ok, label, detail=''):
        self.list.append((bool(ok), label, detail))

    def ran(self):
        for side in ('host', 'guest'):
            code = self.result[side + '_exit']
            self.check(code == 0, 'the %s game ran to the end' % side, 'exit %s' % code)

    def visit(self):
        g = self.guest['events']
        self.check(any(e.startswith('COOPTEST session visit') for e in g), "the guest's visit started",
                   [e for e in g if 'session' in e or 'status' in e][-4:])

    def together(self, kind, where):
        h, g = shown(self.host['coop'], 1, kind), shown(self.guest['coop'], 0, kind)
        self.check(h >= 3, where + ' the host shows the guest', '%d seconds' % h)
        self.check(g >= 3, where + ' the guest shows the host', '%d seconds' % g)

    def same_monsters(self):
        """The joiner's floor holds the host's monsters: the most each counted, and the count at the end, match (a
        monster the host gained after the floor was built, such as a foe the test spawns, is made in the joiner's game
        in the same slot); and the joiner was never out of step with the host."""
        def living(side):
            return [int(s['monsters']) for s in side['drive'] if s.get('context') == 'dungeon' and s.get('monsters', '').isdigit()]
        h, g = living(self.host), living(self.guest)
        self.check(h and g and max(h) == max(g), "the joiner's floor holds as many monsters as the host's at most",
                   'host up to %s, joiner up to %s' % (max(h or [0]), max(g or [0])))
        self.check(h and g and h[-1] == g[-1], 'and as many at the end', 'host %s, joiner %s' % (h[-1:], g[-1:]))
        made = [l.strip() for l in self.guest['lines'] if "co-op: made the leader's monster" in l or "can't be made here" in l]
        out = [l.strip() for l in self.guest['lines'] if 'out of step' in l.lower()]
        self.check(not out, 'the joiner is never out of step with the host', out[:3] + made[:4])

    def no_errors(self):
        bad = [side + ': ' + l.strip() for side in ('host', 'guest') for l in getattr(self, side)['lines']
               if ('moonmod.coop' in l and ('error' in l.lower() or 'traceback' in l.lower()))
               or "can't go there" in l or 'not in a dungeon or a town' in l]
        self.check(not bad, 'no co-op errors or refused follows', bad[:4])


@checks('meet')
def check_meet(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('T', 'in town')
    big = [e for e in host['events'] if ' big_text ' in e]
    c.check(any(re.match(r'DRIVE step \d+ big_text ok', e) for e in big), "a save's text over 64 KiB goes in and comes back whole", big)
    c.no_errors()
    return c.list


def find_key(value, key):
    """The first value under `key` anywhere in a JSON document."""
    if isinstance(value, dict):
        if key in value:
            return value[key]
        for inner in value.values():
            found = find_key(inner, key)
            if found is not None:
                return found
    elif isinstance(value, list):
        for inner in value:
            found = find_key(inner, key)
            if found is not None:
                return found
    return None


def read_save(path):
    try:
        return json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None


def toasts(side):
    return [l.split('toast ', 1)[1] for l in side['lines'] if 'toast ' in l][-6:]


def toast(side, text):
    return any(text in l for l in side['lines'] if 'toast ' in l)


@checks('own')
def check_own(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('T', 'in town')
    c.check(toast(host, 'joins with their own save'), 'the host takes the joiner with their own save', toasts(host))
    gave = [e for e in guest['events'] if ' gilda ' in e]
    c.check(any(re.match(r'DRIVE step \d+ gilda ok', e) for e in gave), 'the joiner gains Gilda in the visit', gave)
    out = Path(result['out'])
    made = sorted((out / 'own-save' / 'saves').glob('*.json'))
    name = made[0].name if made else 'none'
    before, after = read_save(made[0]) if made else None, read_save(out / 'guest' / 'save' / 'saves' / name)
    # (the copy is named <stem>.<date>-<time>-<n>.json, one per run of the game, the newest five kept)
    copies = sorted((out / 'guest' / 'save' / 'moonmod' / 'coop-own-saves').glob(Path(name).stem + '.*' + Path(name).suffix))
    aside = read_save(copies[0]) if copies else None
    c.check(before is not None and after is not None, "the joiner's save is there before and after", name)
    if before is not None and after is not None:
        was, now = find_key(before, 'money'), find_key(after, 'money')
        c.check(isinstance(was, int) and now == was + 4321, 'the Gilda gained in the visit is in the save afterwards', '%s -> %s' % (was, now))
        c.check(find_key(after, 'game_progress') == find_key(before, 'game_progress') == 2,
                "the save's story is where it was, at the gate", '%s -> %s' % (find_key(before, 'game_progress'), find_key(after, 'game_progress')))
        c.check(json.dumps(after.get('towns'), sort_keys=True) == json.dumps(before.get('towns'), sort_keys=True) and
                find_key(after, 'bit_flag') == find_key(before, 'bit_flag') and find_key(after, 'save_dungeon') == find_key(before, 'save_dungeon'),
                'its flags, towns and dungeon records are untouched')
        c.check(aside is not None and find_key(aside, 'money') == was, 'the save as it was is copied aside before the first write',
                find_key(aside, 'money') if aside else 'no copy')
    accounts = read_save(out / 'host' / 'save' / 'moonmod' / 'coop_accounts.json')
    kept = find_key(accounts, 'accounts') if accounts else None
    c.check(not kept, "the host keeps nothing of the joiner's", kept if not kept else len(kept))
    c.no_errors()
    return c.list


def heroes_seen(side):
    """The (room's hero, hero played) pairs a game logged, in order."""
    return [e.split()[2] for e in side['events'] if e.startswith('COOPTEST hero ')]


@checks('heroes')
def check_heroes(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('T', 'in town')
    h, g = heroes_seen(host), heroes_seen(guest)
    c.check('max/max' in h, 'the host plays Max', h)
    c.check('monica/monica' in g, 'the joiner plays Monica', g)
    c.check(any(e == 'COOPTEST swap asking' for e in guest['events']) and any(e.startswith('COOPTEST swap asked -> yes') for e in host['events']),
            'the joiner asks to swap and the host says yes', [e for e in host['events'] + guest['events'] if 'swap' in e])
    c.check(h[-1:] == ['monica/monica'] and 'max/max' in h and h.index('max/max') < len(h) - 1, 'after the swap the host plays Monica', h)
    c.check(g[-1:] == ['max/max'], 'and the joiner plays Max', g)
    c.check(toast(host, 'you play Monica now') and toast(guest, 'you play Max now'), 'each is told who they play', toasts(host) + toasts(guest))
    c.no_errors()
    return c.list


@checks('flags')
def check_flags(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    h = [e for e in host['events'] if e.startswith('DRIVE flag ')]
    g = [e for e in guest['events'] if e.startswith('DRIVE flag ')]
    c.check('DRIVE flag 1500 is true' in h, "a story flag the joiner sets reaches the host's world", h)
    c.check('DRIVE flag 1501 is true' in g, "a story flag the host sets reaches the joiner's game", g)
    c.check('DRIVE flag 1501 is true' in h and 'DRIVE flag 1500 is true' in g, 'each game keeps the flag it set itself', h + g)
    c.no_errors()
    return c.list


@checks('towns')
def check_towns(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()

    def seen(side, town):
        return [e.split()[4] for e in side['events'] if e.startswith('DRIVE town %d is ' % town)]
    h1, h2, g1, g2 = seen(host, 1), seen(host, 2), seen(guest, 1), seen(guest, 2)
    c.check(len(h1) == 2 and h1[0] != h1[1], "the joiner's change to Balance Valley reaches the host's world", h1)
    c.check(len(g1) == 1 and h1[-1:] == g1, 'and the town is the same in both games', h1 + g1)
    c.check(len(h2) == 2 and h2[0] != h2[1] and len(g2) == 1 and h2[-1:] == g2, "the host's change to Veniccio reaches the joiner's game", h2 + g2)
    sent = [e for e in host['events'] + guest['events'] if e.startswith('COOPTEST town ')]
    c.check('COOPTEST town 1 sent' in guest['events'] and 'COOPTEST town 2 sent' in host['events'],
            'each game sends the town it changed, when the game says it changed', sent)
    c.check(sent.count('COOPTEST town 1 sent') == 1 and sent.count('COOPTEST town 2 sent') == 1,
            'and a town that came from the other game is not sent back', sent)
    c.no_errors()
    return c.list


@checks('build')
def check_build(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('T', 'in Sindain')

    def looks(side):
        out = []
        for e in side['events']:
            m = re.match(r'DRIVE georama town=0 placed=(\d+) standing=(\S+)', e)
            if m:
                out.append((int(m.group(1)), m.group(2)))
        return out
    put = [e for e in guest['events'] if e.startswith('DRIVE part placed')]
    c.check(len(put) >= 2, 'the joiner puts up parts in Sindain', [e for e in guest['events'] if 'part' in e][:6])
    h, g = looks(host), looks(guest)
    c.check(len(h) == 2 and h[1][0] > h[0][0], "the host's town holds them (its record)", h)
    c.check(len(h) == 2 and h[0][1].isdigit() and h[1][1] == str(int(h[0][1]) + len(put)),
            'and they stand on its screen, where the host stood all along (the live map)', '%s, %d put up' % (h, len(put)))
    c.check(g and h and g[-1] == h[-1], 'both games show the same town', g + h)
    scenes = [s.get('scene') for s in host['coop'] if s.get('scene', '').startswith(('T', 'H'))]
    c.check(scenes and all(x == scenes[0] for x in scenes), 'the host is never walked out of the town or back into it', sorted(set(scenes)))
    c.check(not any('back to its entrance' in l for l in host['lines']), 'nor told it was sent back to the entrance')
    c.no_errors()
    return c.list


@checks('walk_probe')
def check_walk_probe(host, guest, result):
    seen = [s.get('pos') for s in host['drive'] if s.get('context') in ('town', 'georama')]
    return [(bool(seen), 'the host walked (where it stood each second)', seen)]


@checks('shove')
def check_shove(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('T', 'in Sindain')
    put = [e for e in guest['events'] if e.startswith('DRIVE part placed')]
    c.check(put, 'the joiner puts up a house in Sindain', [e for e in guest['events'] if 'part' in e][:4])

    def pos(s):
        try:
            return [float(v) for v in s.get('pos', '').split(',')]
        except ValueError:
            return None
    stood = [(int(s.get('frame', 0)), pos(s), s.get('walking')) for s in host['drive'] if s.get('context') in ('town', 'georama') and pos(s)]
    moved = [l.strip() for l in host['lines'] if 'co-op: a part landed where the player stood' in l]
    c.check(moved and 'moved to' in moved[0], "in the host's game the house lands where the host stands, and the host is set down beside it",
            moved[:2] or [s[1] for s in stood[-3:]])
    near = [p for _, p, _ in stood if abs(p[0] - SHOVE_SPOT[0]) < 60 and abs(p[2] - SHOVE_SPOT[1]) < 60]
    c.check(near, 'the host stood at the house spot first', [s[1] for s in stood[:6]])
    last = stood[-1:] if stood else []
    c.check(last and last[0][2] == 'true', 'and walks on afterwards', last)
    m = re.search(r'moved to (-?\d+),(-?\d+),(-?\d+)', moved[0]) if moved else None
    if m and last:
        to = [float(v) for v in m.groups()]
        c.check(abs(last[0][1][0] - to[0]) < 40 and abs(last[0][1][2] - to[2]) < 40, 'where it was set down (not pushed back in)',
                'set down at %s, at the end %s' % (to, last[0][1]))
    c.check(not any('back to its entrance' in l for l in host['lines']), 'never sent back to the entrance')
    c.no_errors()
    return c.list


@checks('house_probe')
def check_house_probe(host, guest, result):
    seen = [e for e in host['events'] if e.startswith('DRIVE part placed')]
    return [(bool(seen), 'the house is taken somewhere near the spot', seen[:3])]


@checks('place_probe')
def check_place_probe(host, guest, result):
    seen = [e for e in host['events'] if e.startswith(('DRIVE georama town', 'DRIVE georama anchor', 'DRIVE part', 'DRIVE step'))]
    for e in seen:
        print('   ', e[:230])
    return [(any(e.startswith('DRIVE part placed') for e in seen), 'the town took a part', '')]


@checks('georama_probe')
def check_georama_probe(host, guest, result):
    seen = [e for e in host['events'] if e.startswith('DRIVE georama ')]
    for e in seen:
        print('   ', e)
    return [(bool(seen), "the town's Georama was read", '')]


@checks('gate')
def check_gate(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    said = [e for e in guest['events'] if e.startswith('COOPTEST status')]
    c.check(any('talked to Donny' in e for e in said), 'a joiner with no save past the gate is told why', said[-3:])
    c.check(not any(e.startswith('COOPTEST session visit') for e in guest['events']), 'and no visit starts')
    c.check(not any('role=guest' in e for e in guest['events']), 'nor is the room entered', [e for e in guest['events'] if 'room' in e])
    c.no_errors()
    return c.list


@checks('gate_host')
def check_gate_host(host, guest, result):
    c = Checks(host, guest, result)
    c.check(result['host_exit'] == 0, 'the host game ran to the end', 'exit %s' % result['host_exit'])
    c.check(any(e == 'COOPTEST hosting' for e in host['events']), 'the host asked for a room on the opening floor',
            [e for e in host['events'] if 'COOPTEST' in e][:4])
    c.check(toast(host, 'Co-op opens once you have talked to Donny'), 'and is told co-op opens after the talk with Donny', toasts(host))
    c.check(not any(e.startswith('COOPTEST room') for e in host['events']), 'no room opens')
    return c.list


@checks('floor_first')
def check_floor_first(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.check(any('offer' in e and 'join' in e for e in host['events']), 'the host is offered to follow the joiner onto the floor',
            [e for e in host['events'] if 'offer' in e])
    c.together('D', 'on the floor')
    h_hash = {s.get('layout') for s in host['drive'] if s.get('context') == 'dungeon' and s.get('layout') not in (None, 'nil')}
    g_hash = {s.get('layout') for s in guest['drive'] if s.get('context') == 'dungeon' and s.get('layout') not in (None, 'nil')}
    c.check(h_hash and h_hash == g_hash, 'the floors are the same (layout, chests, monsters)', '%s / %s' % (sorted(h_hash), sorted(g_hash)))
    led = [e for e in guest['events'] if e.startswith('COOPTEST floor ')]
    followed = [e for e in host['events'] if e.startswith('COOPTEST floor ')]
    c.check(any(e.startswith('COOPTEST floor led D') for e in led) and not any('given' in e for e in led),
            'the joiner, there first, runs the floor', led)
    c.check(any(e.startswith('COOPTEST floor followed D') for e in followed), 'the host, coming second, does not', followed)
    c.check(any(s.get('puppets') == 'true' for s in host['coop'] if s.get('scene', '').startswith('D')),
            "the host's monsters copy the joiner's")
    c.check(not any(s.get('puppets') == 'true' for s in guest['coop']), "the joiner's monsters stay its own")
    struck = [e for e in guest['events'] if re.match(r'DRIVE step \d+ strike ok', e)]
    c.check(len(struck) >= 1, 'the joiner struck a monster down before the host came', [e for e in guest['events'] if 'strike' in e])
    sent = [int(s['mons']) for s in guest['coop'] if s.get('scene', '').startswith('D') and s.get('mons', '-') not in ('-', 'nil')]
    heard = [int(s['mons']) for s in host['coop'] if s.get('scene', '').startswith('D') and s.get('mons', '-') not in ('-', 'nil')]
    c.check(sent and heard and max(heard) == max(sent), "the host's copies hold the joiner's living monsters, no more",
            'joiner %s, host %s' % (sorted(set(sent)), sorted(set(heard))))
    c.no_errors()
    return c.list


@checks('down')
def check_down(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('D', 'on the floor')
    struck = [e for e in guest['events'] if e.startswith('COOPTEST struck hit=true')]
    c.check(any(e.endswith('hp=0') for e in struck), "a blow takes the last of the joiner's life", struck[:3])
    c.check(toast(guest, "you're down"), 'the joiner goes down instead of the game ending', toasts(guest))
    c.check(toast(host, 'is down!'), 'the host is told, and how to revive them', toasts(host))
    c.check(toast(guest, 'revived you'), 'the host, holding Revive beside them, gets the joiner up', toasts(guest))
    c.check(toast(host, 'is back on their feet'), 'and hears that it worked', toasts(host))
    c.same_monsters()
    ended = [e for e in guest['events'] if e.startswith('COOPTEST session_ended')]
    states = [s for s in guest['drive'] if s.get('context') == 'dungeon']
    c.check(states and states[-1].get('session') == 'visit', 'the joiner is still on the floor, in the visit, at the end',
            (states[-1:] or ['no state']) + ended[:1])
    c.no_errors()
    return c.list


@checks('clear')
def check_clear(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('D', 'on the floor')

    def living(side):
        return [int(s['monsters']) for s in side['drive'] if s.get('context') == 'dungeon' and s.get('monsters', '').isdigit()]
    h, g = living(host), living(guest)
    c.check(h and max(h) > 0 and h[-1] == 0, 'the host strikes every monster on the floor down', 'from %s to %s' % (max(h or [0]), h[-1:]))
    c.check(g and max(g) > 0 and g[-1] == 0, "and the joiner's floor is empty too", 'from %s to %s' % (max(g or [0]), g[-1:]))

    # The game's own clear (IsEventRun): no monster left in its count (a monster struck down counts until its death
    # has played out), no mimic chest unopened, and the floor-cleared system script run, in each game.
    def counted(side):
        return [(s.get('counted'), s.get('mimics'), s.get('cleared')) for s in side['drive'] if s.get('context') == 'dungeon']
    for name, side in (('host', host), ('joiner', guest)):
        seen = counted(side)
        c.check(seen and seen[-1][0] == '0' and seen[-1][1] == '0', "the %s's game counts no monster and no mimic left" % name,
                'counted, mimics, cleared at the end: %s (at most %s counted)' % (seen[-1:], max((int(x[0]) for x in seen if x[0].isdigit()), default=None)))
        ran = [l.strip() for l in side['lines'] if 'RUN SYS_SCRIPT 1500' in l]
        c.check(ran and seen and seen[-1][2] == 'true', "and the %s's game gives the floor's own clear (system script 1500)" % name,
                '%s; cleared=%s' % (ran[:1] or 'not run', seen[-1][2] if seen else None))
    # (the clear's own scene, play_floor_clear, ends in two messages that wait for a button; a headless game presses
    # none, so after its clear a game stays in that scene: walking is false there, as it is for a player reading it)
    last = [s for s in guest['drive'] if s.get('context') == 'dungeon'][-1:]
    c.check(last and last[0].get('session') == 'visit' and last[0].get('floor') == '2' and
            (last[0].get('walking') == 'true' or last[0].get('cleared') == 'true'),
            'the joiner is still in the visit on the cleared floor (playing, or in its clear scene)', last)
    c.no_errors()
    return c.list


@checks('hurt')
def check_hurt(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('D', 'on the floor')
    blows = [e for e in host['events'] if e.startswith('COOPTEST blow on peer 1')]
    c.check(blows, "in the host's game a monster's blow lands on the joiner's hero", blows[:3])
    struck = [e for e in guest['events'] if e.startswith('COOPTEST struck hit=true')]
    c.check(struck, "and costs the joiner life in their own game", [e for e in guest['events'] if 'struck' in e][:3])
    c.same_monsters()
    c.no_errors()
    return c.list


@checks('look')
def check_look(host, guest, result):
    c = Checks(host, guest, result)
    c.visit()
    c.together('D', 'on the floor')
    return c.list


@checks('look_guest')
def check_look_guest(host, guest, result):
    return check_look(host, guest, result)


@checks('preset')
def check_preset(host, guest, result):
    return check_floor(host, guest, result)


@checks('floor')
def check_floor(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('T', 'in town')
    c.check(any('offer' in e and 'join' in e for e in guest['events']), 'the guest is offered to follow onto the floor',
            [e for e in guest['events'] if 'offer' in e])
    c.together('D', 'on the floor')
    # the same floor: the scene's name carries the seed, and the layout's number must match too once the engine gives it
    h_scenes = {s.get('scene') for s in host['coop'] if s.get('scene', '').startswith('D')}
    g_scenes = {s.get('scene') for s in guest['coop'] if s.get('scene', '').startswith('D')}
    c.check(h_scenes and h_scenes & g_scenes, 'both built the floor from the same seed', '%s / %s' % (sorted(h_scenes), sorted(g_scenes)))
    h_hash = {s.get('layout') for s in host['drive'] if s.get('context') == 'dungeon' and s.get('layout') not in (None, 'nil')}
    g_hash = {s.get('layout') for s in guest['drive'] if s.get('context') == 'dungeon' and s.get('layout') not in (None, 'nil')}
    c.check(h_hash and h_hash == g_hash, 'and the floors are the same (layout, chests, monsters)', '%s / %s' % (sorted(h_hash), sorted(g_hash)))
    c.check(any(s.get('puppets') == 'true' for s in guest['coop'] if s.get('scene', '').startswith('D')),
            "the guest's monsters copy the host's")
    struck = [e for e in host['events'] if re.match(r'DRIVE step \d+ strike ok', e)]
    c.check(len(struck) >= 1, 'the host struck monsters down', [e for e in host['events'] if 'strike' in e])
    sent = [int(s['mons']) for s in host['coop'] if s.get('scene', '').startswith('D') and s.get('mons', '-') not in ('-', 'nil')]
    heard = [int(s['mons']) for s in guest['coop'] if s.get('scene', '').startswith('D') and s.get('mons', '-') not in ('-', 'nil')]
    h_max, g_max = max(sent or [0]), max(heard or [0])
    c.check(h_max > 0 and g_max == h_max, "the guest's copies hold the host's living monsters",
            'host up to %d, guest up to %d' % (h_max, g_max))
    c.check(sent and heard and min(sent) < h_max and min(heard) <= min(sent),
            "a monster the host strikes down is gone from the guest's floor too",
            'host %s..%s, guest %s..%s' % (h_max, min(sent or [0]), g_max, min(heard or [0])))
    c.no_errors()
    return c.list


# --- running --------------------------------------------------------------------------------------------------------

def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def url_ok(url):
    try:
        with urllib.request.urlopen(url, timeout=2) as r:
            return r.status < 500
    except Exception:
        return False


def find_relay(explicit):
    candidates = [explicit] if explicit else []
    candidates += [ROOT / 'coop' / 'relay', ROOT.parent / 'DC_Web' / 'coop' / 'relay', ROOT / 'tools' / 'gauntlet' / '.cache' / 'relay',
                   HOME / 'dc2-check' / 'ChronicleTwo' / 'tools' / 'gauntlet' / '.cache' / 'relay']
    for path in candidates:
        path = Path(path) if path else None
        if path and (path / 'server.mjs').is_file() and (path / 'node_modules' / 'ws').is_dir():
            return path
    return None


def find_data(explicit):
    candidates = [explicit, os.environ.get('DC2_DATA'), ROOT / 'port' / 'run' / 'data', ROOT / 'tools' / 'gauntlet' / '.cache' / 'retail' / 'data',
                  HOME / 'dc2-check' / 'ChronicleTwo' / 'tools' / 'gauntlet' / '.cache' / 'retail' / 'data']
    for path in candidates:
        if not path:
            continue
        path = Path(path)
        # a data root is <game root>/data: the unified directory, or data.zip beside it; data.json sits in the game root
        if (path.parent / 'data.json').is_file() and (path.is_dir() or (path.parent / 'data.zip').is_file()):
            return path
    return None


def find_moonmod(explicit):
    candidates = [explicit, os.environ.get('MOONMOD'), ROOT.parent / 'MoonMod', Path('/mnt/e/Moonmod')]
    for path in candidates:
        if path and (Path(path) / 'mods' / 'moonmod.coop' / 'main.lua').is_file() and \
                (Path(path) / 'tools' / 'tests' / 'coop' / 'live' / 'coopdrive' / 'main.lua').is_file():
            return Path(path)
    return None


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding='utf-8')


def make_own_save(exe, data, out):
    """A save of the joiner's own, made by the game itself: the first dungeon's start (story progress 2, the gate),
    saved to File 1 from its floor list (OWN_SAVE_INPUT). Returns its saves folder, or None."""
    root = out / 'own-save'
    root.mkdir(parents=True)
    env = dict(os.environ)
    env.update({'DC2_SAVE': str(root), 'DC2_DATA': str(data), 'DC2_AUDIO': 'off', 'SDL_VIDEO_DRIVER': 'offscreen',
                'MOONMOD_NO_MODS': '1', 'DC2_RENDER_SCALE': '1'})
    for name in ('DC2_JUMP', 'DC2_GIVE', 'DC2_SEED', 'DC2_HEADLESS_BOUNDED', 'DC2_SKIP_EVENTS'):
        env.pop(name, None)
    (out / 'own_save.input').write_text(OWN_SAVE_INPUT, encoding='utf-8')
    cmd = [str(exe), '--headless', '--offscreen', '--window', '640x480', '--fast-load', '--jump', 'dungeon:0:1',
           '--input', str(out / 'own_save.input'), '--frames', '2400', '--screenshot', str(out / 'own-save.png')]
    with open(out / 'own-save.log', 'w', encoding='utf-8', errors='replace') as log:
        try:
            subprocess.run(cmd, cwd=str(root), env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
        except subprocess.TimeoutExpired:
            return None
    saves = root / 'saves'
    return saves if any(saves.glob('BESCES-*.json')) else None


def make_save(root, moonmod, name, relay_url, autotest, script, own_saves=None, settings=None):
    save = root / 'save'
    if save.exists():
        shutil.rmtree(save)
    (save / 'mods').mkdir(parents=True)
    if own_saves is not None:
        shutil.copytree(own_saves, save / 'saves')
    shutil.copytree(moonmod / 'mods' / 'moonmod.coop', save / 'mods' / 'moonmod.coop')
    shutil.copytree(moonmod / 'tools' / 'tests' / 'coop' / 'live' / 'coopdrive', save / 'mods' / 'coopdrive')
    write_json(save / 'moonmod' / 'profile.json', {
        'enabled': {'moonmod.coop': True, 'coopdrive': True},
        'format_version': 1,
        'native_trusted': {},
        'settings': {'moonmod.coop': dict({'server': relay_url, 'name': name}, **(settings or {}))},
    })
    write_json(save / 'moonmod' / 'data' / 'moonmod.coop' / 'storage.json', {'autotest': autotest})
    write_json(save / 'moonmod' / 'data' / 'coopdrive' / 'storage.json', {'script': script})
    return save


def launch(exe, data, save, jump, frames, log_path, relay_url, input_script=None, extra=()):
    env = dict(os.environ)
    env.update({'DC2_SAVE': str(save), 'DC2_DATA': str(data), 'DC2_HEADLESS_BOUNDED': '1', 'DC2_AUDIO': 'off',
                'SDL_VIDEO_DRIVER': 'offscreen', 'DC2_COOP_RELAY': relay_url, 'DC_COOP_RELAY': relay_url, 'MOONMOD_NO_MODS': '0'})
    for name in ('DC2_JUMP', 'DC2_GIVE', 'DC2_SEED'):
        env.pop(name, None)
    # A small picture: the headless renderer draws on the processor, and two games share it.
    env['DC2_RENDER_SCALE'] = '1'
    cmd = [str(exe), '--headless', '--offscreen', '--window', '640x480', '--fast-load', '--skip-events', '--jump', jump,
           '--frames', str(frames)]
    if input_script:
        cmd += ['--input', str(input_script)]
    cmd += list(extra)
    log = open(log_path, 'w', encoding='utf-8', errors='replace')
    return subprocess.Popen(cmd, cwd=str(save.parent), env=env, stdout=log, stderr=subprocess.STDOUT), log


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--scenario', default='meet', choices=sorted(SCENARIOS))
    ap.add_argument('--build', default=str(ROOT / 'port' / 'build' / 'pc'), help='the PC build directory (darkchronicle lives in it)')
    ap.add_argument('--data', default=None, help='the extracted data root, <game root>/data (else DC2_DATA, else the gauntlet cache)')
    ap.add_argument('--moonmod', default=None, help="MoonMod's checkout (the co-op mod and its test driver; else MOONMOD)")
    ap.add_argument('--relay-dir', default=None, help="the relay's directory (server.mjs with node_modules/ws)")
    ap.add_argument('--node', default=os.environ.get('CT_NODE') if os.path.exists(os.environ.get('CT_NODE', '')) else 'node')
    ap.add_argument('--out', default=None, help='folder for the saves and logs (default: a temp folder)')
    ap.add_argument('--keep', action='store_true', help='keep the folder afterwards')
    ap.add_argument('--shots', action='store_true', help="write each game's last frame as a PNG beside its log")
    args = ap.parse_args()

    exe = Path(args.build) / 'darkchronicle'
    if not exe.is_file():
        print('NOT RUN: no darkchronicle in %s (build it first)' % args.build)
        return 2
    data = find_data(args.data)
    if data is None:
        print('NOT RUN: no extracted retail data (pass --data <game root>/data or set DC2_DATA)')
        return 2
    moonmod = find_moonmod(args.moonmod)
    if moonmod is None:
        print("NOT RUN: no MoonMod checkout with the co-op mod and its test driver (pass --moonmod)")
        return 2
    relay_dir = find_relay(args.relay_dir)
    if relay_dir is None:
        print('NOT RUN: no relay (server.mjs with node_modules/ws); pass --relay-dir')
        return 2

    out = Path(args.out) if args.out else Path(tempfile.mkdtemp(prefix='coop-live-' + args.scenario + '-'))
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    sc = SCENARIOS[args.scenario]()
    guest_walk = WALK_AT % (sc['guest_walk'], sc['guest_walk'] + 14) if sc.get('guest_walk') else ''
    # (or a longer walk: from a frame, the left stick held at x y, for so many frames)
    guest_walk += ''.join('%d %d %d 128 128\n%d\n' % (frame, x, y, frame + frames) for frame, x, y, frames in sc.get('guest_moves', []))
    (out / 'guest_title.input').write_text(GUEST_TITLE_INPUT + guest_walk, encoding='utf-8')
    host_input = None
    if sc.get('host_walk') or sc.get('host_presses'):
        # (the presses, then the walk: an input script's lines go in frame order)
        lines = press_lines(sc.get('host_presses', []))
        if sc.get('host_walk'):
            lines += WALK_AT % (sc['host_walk'], sc['host_walk'] + 14)
        host_input = out / 'host_walk.input'
        host_input.write_text(lines, encoding='utf-8')
    if sc.get('host_input'):
        host_input = out / 'host.input'
        host_input.write_text(sc['host_input'], encoding='utf-8')

    port = free_port()
    relay_url = 'ws://127.0.0.1:%d' % port
    relay_log = open(out / 'relay.log', 'w', encoding='utf-8')
    relay = subprocess.Popen([args.node, 'server.mjs', '--port', str(port), '--host', '127.0.0.1'], cwd=str(relay_dir),
                             stdout=relay_log, stderr=subprocess.STDOUT)
    host = guest = None
    logs = []
    result = {'host_exit': 'not started', 'guest_exit': 'not started', 'out': str(out)}
    try:
        for _ in range(100):
            if url_ok('http://127.0.0.1:%d/health' % port):
                break
            time.sleep(0.2)
        else:
            print('NOT RUN: the relay did not answer /health on port %d' % port)
            return 2

        def shot(side):
            return ('--screenshot', str(out / (side + '.png'))) if args.shots else ()

        host_save = make_save(out / 'host', moonmod, 'Host', relay_url,
                              dict({'role': 'host', 'where': sc.get('host_where', 'town'), 'follow': False}, **sc.get('host_autotest', {})), sc['host'])
        host, hl = launch(exe, data, host_save, sc.get('host_jump', 'town:10'), sc['host_frames'], out / 'host.log', relay_url,
                          input_script=host_input, extra=tuple(sc.get('host_extra', ())) + shot('host'))
        logs.append(hl)
        code = None
        deadline = time.time() + 180
        while time.time() < deadline and host.poll() is None:
            m = re.search(r'COOPTEST room (\S+) role=host', (out / 'host.log').read_text(encoding='utf-8', errors='replace'))
            if m:
                code = m.group(1)
                break
            time.sleep(0.5)
        if not code and not sc.get('expect_no_room'):
            print('the host opened no room: see', out / 'host.log')
            return 1
        if code:
            print('room', code)
        if code and not sc.get('expect_no_room') and not sc.get('solo'):
            own_saves = None
            if sc.get('guest_own', True):  # a joiner brings a save of their own, past the gate
                own_saves = make_own_save(exe, data, out)
                if own_saves is None:
                    print("the joiner's own save could not be made: see", out / 'own-save.log')
                    return 1
            guest_save = make_save(out / 'guest', moonmod, 'Guest', relay_url, dict({'role': 'guest', 'code': code}, **sc.get('guest_autotest', {})),
                                   sc['guest'], own_saves=own_saves, settings=sc.get('guest_settings'))
            guest, gl = launch(exe, data, guest_save, 'title', sc['guest_frames'], out / 'guest.log', relay_url,
                               input_script=out / 'guest_title.input',
                               extra=tuple(sc.get('guest_extra', ())) + shot('guest'))
            logs.append(gl)
        limit = time.time() + max(sc['host_frames'], sc['guest_frames']) / 60 * 8 + 120
        while time.time() < limit and (host.poll() is None or (guest is not None and guest.poll() is None)):
            time.sleep(1)
        for name, p in (('host', host), ('guest', guest)):
            if p is None:
                continue
            if p.poll() is None:
                p.kill()
                result[name + '_exit'] = 'killed after the time limit (still running)'
            else:
                result[name + '_exit'] = p.wait()
    finally:
        for p in (host, guest):
            if p is not None and p.poll() is None:
                p.kill()
        for f in logs:
            f.close()
        relay.terminate()
        try:
            relay.wait(10)
        except subprocess.TimeoutExpired:
            relay.kill()
        relay_log.close()

    h = parse((out / 'host.log').read_text(encoding='utf-8', errors='replace'))
    g = parse((out / 'guest.log').read_text(encoding='utf-8', errors='replace')) if (out / 'guest.log').exists() else parse('')
    failed = 0
    results = CHECKS[args.scenario](h, g, result)
    for ok, label, detail in results:
        print(('  ok   ' if ok else '  FAIL ') + label + ('' if ok or detail == '' else '  [%s]' % (detail,)))
        failed += 0 if ok else 1
    print('%d passed, %d failed  (logs in %s)' % (len(results) - failed, failed, out))
    if not args.keep and failed == 0:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
