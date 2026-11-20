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
import datetime
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


def floor_together(dungeon, floor_id, guest_autotest=None):
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
    sc = {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800}
    if guest_autotest:
        sc['guest_autotest'] = guest_autotest
    return sc


@scenario('floor')
def floor():
    """The guest joins in Palm Brinks, the host walks into the Underground Channel (floor 2, a random layout) and the
    guest follows by its offer; both stand on the same floor, the guest's monsters are copies of the host's, and one
    the host strikes down is gone from the guest's floor too. The offer is held open 300 frames first (asked in the
    game's own Yes/No window), so a screenshot shows it."""
    return floor_together(0, 2, {'offer_hold': 300})


@scenario('map')
def map_():
    """The other player on the floor's map: as floor, and once on the floor the guest walks off ahead, so the two
    stand apart. The host plays with the plain map (the aerial map off in its settings), so the guest is the hero's
    arrow in its player's colour there; the guest keeps the aerial map, so the host is its hero's model there. Each
    game starts with the small map; at frame 3300 each presses Select, the game's own way to the large one. With
    --shots-every 300 the frames from 3600 on show the large maps with the two apart (each game counts its own frames,
    and the guest's walk comes later on the host's clock)."""
    sc = floor_together(0, 2)
    sc['guest_moves'] = [(2600, 128, 0, 150)]
    sc['host_presses'] = [(3300, 'select')]
    sc['guest_presses'] = [(3300, 'select')]
    sc['host_config'] = {'game': {'aerial_map': False}}
    return sc


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


@scenario('reward')
def reward():
    """A kill pays both players as the game pays its own. The two stand on a floor, two foes of the test's own (a
    sewer rat's kind) come beside them, and the host strikes both down where the two stand. In the joiner's game each
    copy dies as the game's own monster dies there (its death program runs and tells mods: the burst, the orbs, what
    it drops), and each hero standing by gathers the orbs: the absorption (ABS) of their weapons goes up in both
    games."""
    host = [
        {'when': 'town', 'after': 1800, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 2},
        {'when': 'playing', 'after': 600, 'op': 'abs_log'},
        {'after': 30, 'op': 'spawn_foe', 'dx': 40},
        {'after': 30, 'op': 'spawn_foe', 'dx': -40},
        {'after': 240, 'op': 'strike_near', 'radius': 120},
        {'after': 90, 'op': 'strike_near', 'radius': 120},
        {'after': 600, 'op': 'abs_log'},
        {'after': 600, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'dungeon', 'after': 300, 'op': 'abs_log'},
        {'after': 4200, 'op': 'abs_log'},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800,
            'host_extra': ['--invincible']}


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


@scenario('build_edit')
def build_edit():
    """Georama seen by both while one of them is in the editor: the host opens Sindain's editor (Select) and stays in
    it, the joiner puts up houses and blocks, and the host's editor shows each of them as it comes, without the host
    leaving the editor (the town is built again between the editor's actions)."""
    house, block = {'template': 47, 'town': 0}, {'template': 36, 'town': 0}
    host = [
        {'when': 'town', 'after': 30, 'op': 'progress', 'value': 4},
        dict({'after': 570, 'op': 'georama_probe'}, **house),
        dict({'after': 10, 'op': 'mark', 'text': 'knows the block'}, **block),
        {'after': 1500, 'op': 'georama_probe', 'town': 0},
        {'after': 3000, 'op': 'georama_probe', 'town': 0},
        {'after': 300, 'op': 'mark', 'text': 'end'},
    ]
    # (the joiner builds well after the host pressed Select at its frame 1300: the two games' clocks start apart, and a
    # joiner building at 900 could reach the host while its editor was still opening; probed right after building too)
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        dict({'when': 'town', 'after': 1500, 'op': 'part_place', 'spots': [[-300, -320], [300, -320]]}, **house),
        dict({'after': 120, 'op': 'part_place', 'spots': [[0, -440], [-200, -120], [200, -120]]}, **block),
        {'after': 30, 'op': 'georama_probe', 'town': 0},
        {'after': 600, 'op': 'georama_probe', 'town': 0},
    ]
    # (the story save town:0 jumps into has Georama shut at that point of the story; the game's own Georama debug save,
    # georama:0, has it open but is before the co-op gate, so the host's progress is raised first and the room opened
    # after it. The host walks off the town's way in, then presses Select.)
    return {'host': host, 'guest': guest, 'host_jump': 'georama:0', 'host_frames': 7200, 'guest_frames': 6600,
            'host_autotest': {'at': 400}, 'host_input': WALK_RIGHT % SHOVE_WALK + press_lines([(1300, 'select')])}


@scenario('build_edit_opening')
def build_edit_opening():
    """build_edit with the joiner building while the host's editor is still opening (Select pressed, the editor not
    yet taking towns): every part the joiner put up stands in both games at the end."""
    sc = build_edit()
    sc['guest'][1]['after'] = 900
    return sc


# (the moment the editor opens in the host's game falls differently against the joiner's clock from run to run: these
# start the joiner's building earlier and later, so that one of them lands in it)
def _opening_at(after):
    def make():
        sc = build_edit()
        sc['guest'][1]['after'] = after
        return sc
    return make


for _after in (450, 600, 750, 1050):
    SCENARIOS['build_edit_opening_%d' % _after] = _opening_at(_after)


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


@scenario('boss')
def boss():
    """A boss's room entered together. The two are in Palm Brinks; the host walks into the Underground Channel's
    boss's room (floor 7, the map d01b01, its intro already seen: story flag 10, so the room's way in is its short
    one). The joiner is asked "The host faces the boss of the Underground Channel: join the fight?" and joins; the
    host's fight waits for it meanwhile (monsters.hold: the host's hero and the room stand still), and goes on once the
    joiner has walked in. With two in the room the room's monsters are scaled for two, in both games. (The boss itself
    comes out in the room's own scene, which starts where the hero walks up to it: a headless game does not walk there.)"""
    host = [
        {'when': 'town', 'after': 600, 'op': 'flag_set', 'id': 10},
        {'after': 1200, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 7},
        {'when': 'playing', 'after': 60, 'op': 'world_log'},
        {'after': 600, 'op': 'world_log'},
        {'after': 2400, 'op': 'world_log'},
        {'after': 600, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'dungeon', 'after': 600, 'op': 'world_log'},
        {'after': 1800, 'op': 'world_log'},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800,
            'guest_autotest': {'offer_hold': 240}}


@scenario('boss_fight')
def boss_fight():
    """The boss's fight together. As boss, but the host walks into the Underground Channel's boss's room for the
    first time: the room's own intro plays (skipped, as --skip-events skips it) and places the boss, Halloween, whose
    first hint waits for a button (Cross, pressed through it). The joiner is asked to join the fight and joins; then
    the host strikes the boss, a little at a time, and at last strikes it down. Both games log the floor's monsters
    as they go (monsters_log), and what the world says."""
    host = [
        {'when': 'town', 'after': 600, 'op': 'mark', 'text': 'in town'},
        {'after': 1200, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 7},
        {'when': 'dungeon', 'after': 300, 'op': 'world_log'},
        {'after': 1, 'op': 'monsters_log'},
    ]
    for _ in range(4):
        host += [{'after': 600, 'op': 'world_log'}, {'after': 1, 'op': 'monsters_log'}]
    for _ in range(3):
        host += [{'after': 300, 'op': 'strike', 'amount': 40}, {'after': 120, 'op': 'monsters_log'}]
    host += [{'after': 300, 'op': 'strike_all'}, {'after': 60, 'op': 'monsters_log'}, {'after': 1, 'op': 'world_log'}]
    for _ in range(4):
        host += [{'after': 600, 'op': 'world_log'}, {'after': 1, 'op': 'monsters_log'}]
    host += [{'after': 60, 'op': 'mark', 'text': 'end'}]
    guest = [{'when': 'visit', 'op': 'mark', 'text': 'arrived'}, {'when': 'dungeon', 'after': 300, 'op': 'world_log'},
             {'after': 1, 'op': 'monsters_log'}]
    for _ in range(12):
        guest += [{'after': 300, 'op': 'world_log'}, {'after': 1, 'op': 'monsters_log'}]
    sc = {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 10800, 'guest_frames': 10200,
          'guest_autotest': {'offer_hold': 240}, 'host_extra': ['--invincible'], 'guest_extra': ['--invincible']}
    # (the host's Cross through the hints: in a fight it swings the sword, and a held hero does nothing with it; the
    # joiner presses nothing, as Cross in a town talks to whoever is near: its way into the room is the short one, the
    # intro's flag set by the host's game by then)
    sc['host_presses'] = [(f, 'cross') for f in range(2100, 6000, 150)]
    return sc


@scenario('robomb')
def robomb():
    """Halloween's RoBombs thrown back at it, by the joiner. As boss_fight, but nobody strikes the boss: the host
    presses Cross through the intro's hint and no more, the joiner walks up to the host (Halloween lobs its RoBombs
    at whoever is nearest) and tries the lift every few seconds (R1 held, Cross; Cross again throws). A RoBomb the
    joiner lifts is held and thrown by its avatar in the host's game, where it lands, blows up and hurts Halloween;
    in the joiner's game a RoBomb's blast is only drawn, the host's game's blow on it coming from there."""
    host = [
        {'when': 'town', 'after': 600, 'op': 'mark', 'text': 'in town'},
        {'after': 1200, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 7},
        {'when': 'dungeon', 'after': 300, 'op': 'monsters_log'},
    ]
    for _ in range(36):
        host += [{'after': 200, 'op': 'monsters_log'}, {'after': 1, 'op': 'monsters_where'}]
    host += [{'after': 60, 'op': 'mark', 'text': 'end'}]
    guest = [{'when': 'visit', 'op': 'mark', 'text': 'arrived'}, {'when': 'dungeon', 'after': 300, 'op': 'monsters_log'}]
    for _ in range(30):
        guest += [{'after': 200, 'op': 'monsters_log'}, {'after': 1, 'op': 'monsters_where'}]
    # (each try, from where the walk left the joiner: R1 held, Cross lifts; Circle is the game's lock-on, and a hero
    # standing with a monster in its hands turns to what it is locked on to (CActionChara::HumanShrowMoveIF), but in
    # the runs so far the throws went straight ahead; Cross throws, or swings when nothing was lifted, a step on each
    # time. COOP.md has what the runs saw.)
    holds = []
    for start in range(4040, 10800, 160):
        holds += [(start, 'r1'), (start + 30, 'r1 cross'), (start + 38, 'r1'), (start + 46, ''),
                  (start + 56, 'circle'), (start + 62, ''), (start + 110, 'cross'), (start + 116, '')]
    sc = {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 12000, 'guest_frames': 11400,
          'guest_autotest': {'offer_hold': 240}, 'host_extra': ['--invincible'], 'guest_extra': ['--invincible'],
          'guest_holds': holds}
    # (the host through the hint only, then still: a swing of its own would hurt the boss too)
    sc['host_presses'] = [(f, 'cross') for f in range(2100, 3900, 150)]
    # (the joiner, in at the room's door at 841,-161 by 3400 of the input script's frames (they run some 600 behind the
    # mod's), stands there until 3900 and then walks ahead, the stick held up, along the line toward the place
    # Halloween fights from (about 145,60), some 5.5 a frame: 110 frames stop it beside the host, some 130 short of
    # the boss, where the RoBombs fall)
    sc['guest_moves'] = [(3900, 128, 0, 110)]
    return sc


@checks('robomb')
def check_robomb(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()

    def lines(side, pattern):
        return [l.strip() for l in side['lines'] if re.search(pattern, l)]
    lifted = lines(guest, r"co-op: this game's hero lifted the copy in slot \d+ \(kind 1000\)")
    c.check(lifted, "the joiner's hero lifts a RoBomb's copy", lifted[:2] or lines(guest, 'lifted the copy')[:2])
    held = lines(host, r'co-op: avatar \d lifted the monster in slot \d+ \(kind 1000\)')
    c.check(held, "the host's game holds its RoBomb in the joiner's hero's hands", held[:2] or lines(host, "can't lift")[:2])
    threw = lines(host, r'co-op: avatar \d threw the monster in slot \d+')
    c.check(threw, "and throws it", threw[:2] or lines(host, 'put down|let go')[:2])

    # Halloween's life (kind 500), as the monsters_log steps saw it: (hp, max, whether a throw came before it)
    def boss_life(side, mark=None):
        out, thrown = [], False
        for l in side['lines']:
            if mark is not None and mark in l:
                thrown = True
            if 'DRIVE monsters n=' in l:
                for _, hp, top in re.findall(r'\d+:(500):(\d+)/(\d+)', l):
                    out.append((int(hp), int(top), thrown))
        return out
    life = boss_life(host, 'threw the monster in slot')
    before = [hp for hp, _, thrown in life if not thrown]
    after = [hp for hp, _, thrown in life if thrown]
    c.check(before and after and after[-1] < before[-1], "a RoBomb thrown back hurts Halloween in the host's game",
            {'before the first throw': before[-1:], 'after': after[:1] + after[-1:]})
    there = boss_life(guest)
    c.check(there and after and there[-1][0] < there[-1][1], "and its copy in the joiner's game has the life it lost",
            there[-1:])
    drawn = lines(guest, r'co-op: what the copy in slot \d+ \(kind 1000\) made is only drawn here')
    c.check(drawn, "in the joiner's game a RoBomb copy's blast is only drawn", drawn[:2])
    c.no_errors()
    return c.list


def monster_lines(side):
    """The monsters_log steps' lines: per line, {slot: (kind, hp, max)} of the living monsters."""
    out = []
    for e in side['events']:
        if e.startswith('DRIVE monsters n='):
            seen = {}
            for slot, kind, hp, top in re.findall(r'(\d+):(\d+):(\d+)/(\d+)', e):
                seen[int(slot)] = (int(kind), int(hp), int(top))
            out.append(seen)
    return out


@checks('boss_fight')
def check_boss_fight(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    HALLOWEEN = 500
    hm, gm = monster_lines(host), monster_lines(guest)
    h_boss = [m[0] for m in hm if m.get(0, (None,))[0] == HALLOWEEN]
    g_boss = [m[0] for m in gm if m.get(0, (None,))[0] == HALLOWEEN]
    c.check(h_boss and h_boss[0][2] == 256, "the room's intro brings the boss out in the host's game (Halloween, 256 life)", h_boss[:1])
    offers = [e for e in guest['events'] if 'offer' in e]
    c.check(any('faces the boss of the Underground Channel' in e and 'join' in e for e in offers),
            'the joiner is asked to join the fight, and joins', offers)
    c.check(toast(host, 'the fight waits for') and toast(host, 'joins the fight'),
            "the host's fight waits for the joiner, who joins it", toasts(host))
    c.together('D', "in the boss's room")
    c.check(any(b[2] == 448 for b in h_boss), "with two in the fight the boss has 75% more life (scale_bosses): 448",
            sorted({b[2] for b in h_boss}))
    made = [l.strip() for l in guest['lines'] if "made the leader's monster in slot 0 (kind 500)" in l]
    c.check(made and g_boss, "the boss is in the joiner's game too, its copy of the host's", (made[:1], g_boss[:1]))
    c.check(g_boss and any(b[2] == 448 for b in g_boss), "with the same life", sorted({b[2] for b in g_boss}))
    c.check(h_boss and min(b[1] for b in h_boss) < 448 and g_boss and min(b[1] for b in g_boss) < 448,
            "the host's blows take its life in both games", (min([b[1] for b in h_boss] or [0]), min([b[1] for b in g_boss] or [0])))
    for name, side, seen in (('host', host, hm), ('joiner', guest, gm)):
        c.check(seen and not seen[-1], "at last the boss falls in the %s's game" % name, seen[-1:])
    ran = {name: [l.strip() for l in side['lines'] if l.startswith('RUN SYS_SCRIPT ')] for name, side in (('host', host), ('joiner', guest))}
    c.check('RUN SYS_SCRIPT 520' in ran['host'] and 'RUN SYS_SCRIPT 1500' in ran['host'],
            "the host's game plays the boss's end, the story's scene (event 520), and the floor's clear (1500)", ran['host'])
    # (a boss's room has no floor's clear of its own: its scene loads the dungeon's script, which has it, and that
    # scene is the host's; the joiner's room counts no monster, and the game says cleared)
    gw = world_lines(guest)
    c.check('RUN SYS_SCRIPT 520' not in ran['joiner'] and gw and gw[-1].get('floor_cleared') == 'true',
            "the joiner's game plays no scene (its copy asks for none: the story is the host's), and its room is cleared",
            (ran['joiner'], gw[-1:]))
    died = [e for e in guest['events'] if e.startswith('DRIVE defeated') and 'kind=500' in e]
    c.check(died, "in the joiner's game the boss's copy dies as the game's own boss dies", died[:2])
    c.no_errors()
    return c.list


@scenario('town_talk')
def town_talk():
    """A player busy in a town is still in the room. Both in Palm Brinks; the joiner presses Cross every few seconds for
    a minute and a half (in the first boss_fight run that started the town's event 400 where it stood, and the host
    was told the joiner's connection had dropped). The room holds both until the joiner's game ends (its frames run
    out before the host's: the room hears it go then, as a dropped connection, and not before)."""
    host = [{'when': 'town', 'after': 600, 'op': 'mark', 'text': 'in town'}]
    for _ in range(8):
        host += [{'after': 600, 'op': 'world_log'}]
    host += [{'after': 600, 'op': 'mark', 'text': 'end'}]
    guest = [{'when': 'visit', 'op': 'mark', 'text': 'arrived'}]
    for _ in range(8):
        guest += [{'after': 600, 'op': 'world_log'}]
    sc = {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 7800, 'guest_frames': 7200}
    sc['guest_presses'] = [(f, 'cross') for f in range(2100, 7000, 150)]
    return sc


@checks('town_talk')
def check_town_talk(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    talks = [l.strip() for l in guest['lines'] if l.startswith('event = ')]
    c.check(talks, "the joiner's Cross starts a talk in the town", talks[:2])
    # (the relay's clock and the joiner's log's last write, the moment its game ended: a leave seconds before that
    # is a drop)
    out = Path(result['out'])
    relay = (out / 'relay.log').read_text(encoding='utf-8', errors='replace').splitlines() if (out / 'relay.log').is_file() else []
    left = [l for l in relay if re.search(r'player \d+ left', l)]
    ended = (out / 'guest.log').stat().st_mtime if (out / 'guest.log').is_file() else 0.0

    def at(line):
        try:
            return datetime.datetime.fromisoformat(line.split(' ', 1)[0].replace('Z', '+00:00')).timestamp()
        except ValueError:
            return 0.0
    early = [l for l in left if at(l) < ended - 5.0]
    c.check(len(left) <= 1 and not early, "the joiner leaves the room only when its game ends", left[:2] + ['game ended %.0f' % ended])
    gone = [t for t in toasts(guest) if 'dropped' in t or 'getting back' in t]
    c.check(not gone, "the joiner's link never drops", gone[:3])
    c.no_errors()
    return c.list


@scenario('lift')
def lift():
    """A monster of the host's floor lifted by the joiner and thrown. Both on a floor, a foe of the test's own (a sewer
    rat's kind) comes to them; the joiner guards (R1) and presses Cross, the game's lift, and Cross again throws what
    it holds, tried every few seconds. In the joiner's game the copy is held at once, thrown and lands; the host's
    game is told, its monster is held in the joiner's hero's hands there and thrown the same way; the copy follows
    the host's monster again once the host's reports have it free."""
    host = [
        {'when': 'town', 'after': 1800, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 2},
        {'when': 'playing', 'after': 600, 'op': 'spawn_foe', 'dx': 10, 'attack': 1},
    ]
    for _ in range(8):
        host += [{'after': 600, 'op': 'monsters_log'}]
    host += [{'after': 300, 'op': 'mark', 'text': 'end'}]
    guest = [{'when': 'visit', 'op': 'mark', 'text': 'arrived'}, {'when': 'dungeon', 'after': 300, 'op': 'monsters_log'}]
    for _ in range(8):
        guest += [{'after': 600, 'op': 'monsters_log'}]
    holds = []
    for start in range(3000, 7000, 420):
        # R1 held, the guard up; Cross while it holds (Max's lift takes it held, Monica's pressed); then let go of
        # both, and Cross again a moment later: the throw, when something is held (a swing, when not)
        holds += [(start, 'r1'), (start + 40, 'r1 cross'), (start + 48, 'r1'), (start + 60, ''), (start + 150, 'cross'), (start + 156, '')]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 8400, 'guest_frames': 7800, 'guest_holds': holds,
            'host_extra': ['--invincible'], 'guest_extra': ['--invincible']}


@checks('lift')
def check_lift(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()

    def lines(side, pattern):
        return [l.strip() for l in side['lines'] if re.search(pattern, l)]
    lifted = lines(guest, r"co-op: this game's hero lifted the copy in slot \d+")
    c.check(lifted, "the joiner's hero lifts a copy of the host's monster", lifted[:2])
    held = lines(host, r'co-op: avatar \d lifted the monster in slot \d+')
    c.check(held, "the host's game holds its monster in the joiner's hero's hands", held[:2] or lines(host, "can't lift")[:2])
    thrown = lines(guest, r'co-op: the copy in slot \d+ let go \(thrown\)')
    c.check(thrown, "the joiner throws it", thrown[:2] or lines(guest, 'let go')[:2])
    threw = lines(host, r'co-op: avatar \d threw the monster in slot \d+')
    c.check(threw, "the host's game throws its monster the same way", threw[:2] or lines(host, 'put down|let go')[:2])
    landed = lines(guest, r'co-op: the copy in slot \d+ landed')
    c.check(landed, "in the joiner's game it flies and lands", landed[:2])
    back = lines(guest, r"co-op: the copy in slot \d+ is the leader's again")
    c.check(back and all('its reports have it free' in l for l in back), "and follows the host's monster again once the host's reports have it free",
            back[:3])
    c.no_errors()
    return c.list


@scenario('scale')
def scale_():
    """Harder with two. As floor, but the host first plays the floor's monsters alone: it goes into the Underground
    Channel's floor 2 while the joiner stays in Palm Brinks a minute before following. Both games build the floor
    larger (larger_floors +50%: more rooms on more ground) and fuller (more_monsters +50%), alike; while the host is
    alone there its monsters have their own life, and once the joiner stands there too they have 60% more, in both
    games (scale_life)."""
    host = [
        {'when': 'town', 'after': 1800, 'op': 'enter_floor', 'dungeon_id': 0, 'floor_id': 2},
        {'when': 'playing', 'after': 300, 'op': 'world_log'},
        {'after': 3600, 'op': 'world_log'},
        {'after': 600, 'op': 'mark', 'text': 'end'},
    ]
    guest = [
        {'when': 'visit', 'op': 'mark', 'text': 'arrived'},
        {'when': 'dungeon', 'after': 1200, 'op': 'world_log'},
    ]
    return {'host': host, 'guest': guest, 'host_jump': 'town:10', 'host_frames': 9000, 'guest_frames': 8400,
            'guest_autotest': {'offer_hold': 1800}}


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


def world_lines(side):
    """The world_log steps' lines, as dicts of their fields."""
    return [dict(STATE_RE.findall(e[len('DRIVE world '):])) for e in side['events'] if e.startswith('DRIVE world ')]


def floor_hp(side):
    """The living monsters' life on the floor, each second this game was on it."""
    return [int(s['hp']) for s in side['drive'] if s.get('context') == 'dungeon' and s.get('hp', '').isdigit()]


@checks('boss')
def check_boss(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    hw, gw = world_lines(host), world_lines(guest)
    room = [w for w in hw if w.get('floor_map') == 'd01b01']
    c.check(room and all(w.get('boss_floor') == 'true' and 'floor_seed' not in w for w in room),
            "the host's game is in the boss's room, its own map, with no seed", room[:1])
    offers = [e for e in guest['events'] if 'offer' in e]
    c.check(any('faces the boss of the Underground Channel' in e and 'join' in e for e in offers),
            'the joiner is asked to join the fight, and joins', offers)
    c.check(toast(host, 'the fight waits for'), "the host's fight waits for the joiner", toasts(host))
    c.check(any(w.get('fight_held') == 'true' for w in hw), 'the boss and the host stand still meanwhile (monsters.hold)',
            [w.get('fight_held') for w in hw])
    c.check(toast(host, 'joins the fight'), 'the host hears the joiner join the fight', toasts(host))
    c.check(hw and hw[-1].get('fight_held') is None, 'and the fight goes on', hw[-1:])
    c.check(any(w.get('floor_map') == 'd01b01' for w in gw), "the joiner's game is in the same room", gw[:1])
    c.together('D', "in the boss's room")
    for name, side in (('host', host), ('joiner', guest)):
        said = [l.strip() for l in side['lines'] if 'co-op: monsters for 2' in l]
        c.check(said and '160 125 175 125' in said[0], "the %s's game scales the room's monsters for two" % name, said[:1])
    c.no_errors()
    return c.list


@checks('scale')
def check_scale(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('D', 'on the floor')
    h_hash = {s.get('layout') for s in host['drive'] if s.get('context') == 'dungeon' and s.get('layout') not in (None, 'nil')}
    g_hash = {s.get('layout') for s in guest['drive'] if s.get('context') == 'dungeon' and s.get('layout') not in (None, 'nil')}
    c.check(h_hash and h_hash == g_hash, 'both games build the one floor (layout, chests, monsters)', '%s / %s' % (sorted(h_hash), sorted(g_hash)))
    hw, gw = world_lines(host), world_lines(guest)
    sizes = [(w.get('floor_rooms'), w.get('floor_grid'), w.get('monsters_added')) for w in hw + gw if w.get('floor_grid')]
    c.check(sizes and all(int(r) >= 6 and g == '21' for r, g, _ in sizes),
            'the floor is larger: 6 rooms or more (the game makes 4 to 6) on a 21-square grid (the game: 14)', sizes)
    c.check(sizes and all(int(a) > 0 for _, _, a in sizes) and len({a for _, _, a in sizes}) == 1,
            'and holds more monsters, as many more in each game', sizes)
    h, g = floor_hp(host), floor_hp(guest)
    alone = h[0] if h else 0
    c.check(h and alone > 0 and max(h) >= alone * 1.5, "once the joiner is there too, the host's monsters have 60% more life",
            'host %s alone, then up to %s' % (alone, max(h or [0])))
    c.check(g and max(g) == max(h or [0]), "and the joiner's copies have the same", 'joiner up to %s' % max(g or [0]))
    for name, side in (('host', host), ('joiner', guest)):
        said = [l.strip() for l in side['lines'] if 'co-op: monsters for 2' in l]
        c.check(said and '160 125 175 125' in said[0], "the %s's game scales its monsters for two" % name, said[:1])
    c.no_errors()
    return c.list


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
    # (a joiner's town goes numbered: "COOPTEST town 1 sent #1")
    def sends(side, town):
        return [e for e in side['events'] if re.fullmatch(r'COOPTEST town %d sent( #\d+)?' % town, e)]
    sent = [e for e in host['events'] + guest['events'] if e.startswith('COOPTEST town ')]
    c.check(sends(guest, 1) and sends(host, 2), 'each game sends the town it changed, when the game says it changed', sent)
    c.check(len(sends(guest, 1) + sends(host, 1)) == 1 and len(sends(guest, 2) + sends(host, 2)) == 1,
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


def georama_looks(side):
    """Each georama_probe of town 0 a side made: (parts in its record, parts standing, in the editor)."""
    out = []
    for e in side['events']:
        m = re.match(r'DRIVE georama town=0 placed=(\d+) standing=(\S+).* editing=(\S+)', e)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3)))
    return out


@checks('build_edit_opening')
def check_build_edit_opening(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('T', 'in Sindain')
    put = [e for e in guest['events'] if e.startswith('DRIVE part placed')]
    c.check(len(put) >= 2, 'the joiner puts up parts in Sindain', [e for e in guest['events'] if 'part' in e][:6])
    h, g = georama_looks(host), georama_looks(guest)
    start = int(h[0][1]) if h and h[0][1].isdigit() else -1
    want = str(start + len(put))
    c.check(len(h) == 3 and h[-1][2] == 'true', 'the host opened the editor and is still in it at the end', h)
    c.check(g and g[0][1] == want, "right after building, the joiner's town holds every part it put up", '%s, want %s' % (g, want))
    c.check(h and h[-1][1] == want and h[-1][0] == int(want), "and so does the host's at the end (its record and its editor)",
            '%s, want %s' % (h, want))
    c.check(g and g[-1][1] == want, "and the joiner's still does at the end", '%s, want %s' % (g, want))
    c.no_errors()
    return c.list


for _after in (450, 600, 750, 1050):
    CHECKS['build_edit_opening_%d' % _after] = check_build_edit_opening


@checks('build_edit')
def check_build_edit(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('T', 'in Sindain')
    looks = georama_looks
    put = [e for e in guest['events'] if e.startswith('DRIVE part placed')]
    c.check(len(put) >= 2, 'the joiner puts up parts in Sindain', [e for e in guest['events'] if 'part' in e][:6])
    h, g = looks(host), looks(guest)
    c.check(len(h) == 3 and h[0][2] == 'false' and h[-1][2] == 'true', 'the host opened the editor and is still in it at the end', h)
    c.check(len(h) == 3 and h[-1][0] > h[0][0], "the host's town holds the joiner's parts (its record)", '%s, %d put up' % (h, len(put)))
    c.check(len(h) == 3 and h[0][1].isdigit() and h[-1][1] == str(int(h[0][1]) + len(put)),
            "and they stand in the host's editor (its live map), the host never leaving it", '%s, %d put up' % (h, len(put)))
    rebuilt = [l.strip() for l in host['lines'] if 'built again in its editor' in l]
    c.check(rebuilt, 'the town was built again inside the editor', rebuilt[:3])
    c.check(g and h and g[-1][:2] == h[-1][:2], 'both games show the same town', g + h)
    c.check(not any('back to its entrance' in l for l in host['lines']), 'the host is not sent back to the entrance')
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
    # Never more than the joiner had (its co-op line counts only once someone follows, so its own count is read too:
    # a host that comes a moment before the joiner's strike hears the floor as it stood before it), and the same at
    # the end.
    sent = [int(s['mons']) for s in guest['coop'] if s.get('scene', '').startswith('D') and s.get('mons', '-') not in ('-', 'nil')]
    own = [int(s['monsters']) for s in guest['drive'] if s.get('context') == 'dungeon' and s.get('monsters', '').isdigit()]
    heard = [int(s['mons']) for s in host['coop'] if s.get('scene', '').startswith('D') and s.get('mons', '-') not in ('-', 'nil')]
    c.check(sent and heard and max(heard) <= max(sent + own) and heard[-1] == sent[-1],
            "the host's copies hold the joiner's living monsters, no more",
            'joiner %s (its own count %s), host %s; at the end %s / %s' % (sorted(set(sent)), sorted(set(own)), sorted(set(heard)),
                                                                     sent[-1:], heard[-1:]))
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


@checks('reward')
def check_reward(host, guest, result):
    c = Checks(host, guest, result)
    c.ran()
    c.visit()
    c.together('D', 'on the floor')
    struck = [e for e in host['events'] if re.match(r'DRIVE step \d+ strike_near ok', e)]
    c.check(len(struck) >= 2, 'the host strikes down the two foes beside the heroes', [e for e in host['events'] if 'strik' in e][:3])
    died = [e for e in guest['events'] if e.startswith('DRIVE defeated')]
    c.check(died, "in the joiner's game its copy dies as the game's own monster dies (the death program runs)", died[:3])

    def abs_seen(side):
        return [float(s['abs']) for s in side['drive'] if s.get('context') == 'dungeon' and 'abs' in s]
    for name, side in (('host', host), ('joiner', guest)):
        seen = abs_seen(side)
        c.check(len(seen) >= 2 and seen[-1] > seen[0], "the %s's weapons gather the kill's absorption (ABS)" % name,
                'from %s to %s' % (seen[:1], seen[-1:]))
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


@checks('map')
def check_map(host, guest, result):
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
    ap.add_argument('--shots-every', type=int, default=0, metavar='N',
                    help="with --shots: also write every Nth frame (host-frame-F.png, guest-frame-F.png)")
    ap.add_argument('--slow', type=float, default=1.0, metavar='X',
                    help='give the games X times the usual wall time (a slower machine, or several runs at once)')
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
    # (or buttons held from a frame on, as many as the line names, none for a bare frame: a guard held while Cross goes)
    guest_walk += ''.join(('%d %s\n' % (frame, buttons)) if buttons else '%d\n' % frame for frame, buttons in sc.get('guest_holds', []))
    # (the presses and the walk in frame order: a press must not fall inside a walk)
    guest_after = sorted((guest_walk + press_lines(sc.get('guest_presses', []))).splitlines(keepends=True),
                         key=lambda line: int(line.split()[0]))
    (out / 'guest_title.input').write_text(GUEST_TITLE_INPUT + ''.join(guest_after), encoding='utf-8')
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
            if not args.shots:
                return ()
            every = ('--screenshot-every', str(args.shots_every)) if args.shots_every > 0 else ()
            return ('--screenshot', str(out / (side + '.png'))) + every

        host_save = make_save(out / 'host', moonmod, 'Host', relay_url,
                              dict({'role': 'host', 'where': sc.get('host_where', 'town'), 'follow': False}, **sc.get('host_autotest', {})), sc['host'],
                              settings=sc.get('host_settings'))
        if sc.get('host_config'):  # (the game's settings file: beside its saves folder)
            (host_save.parent / 'config.json').write_text(json.dumps(sc['host_config']), encoding='utf-8')
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
        limit = time.time() + (max(sc['host_frames'], sc['guest_frames']) / 60 * 8 + 120) * args.slow
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
