# DOAXBV swimsuits

Every woman in DOA3 (Kasumi, Ayane, Christie, Tina, Hitomi, Helena and Lei
Fang) can wear any of her swimsuits from _Dead or Alive Xtreme Beach
Volleyball_, switched live during a fight. Each fighter keeps her own DOA3
head, hair, face and animation; from the neck down she wears her DOAXBV body
in the chosen suit. Lisa has no DOA3 fighter.

The swimsuits are built on the fly from **your own DOAXBV disc**. No DOAXBV
data ships: the suit recipes (`recipes/doaxbv-suits.pack.gz`, in this
repository) hold only numbers and locations on the disc (see [How it works](#how-it-works)).

## Getting the swimsuits

You need DOA3 built and playing (Japan 3.1 or USA 3.0), and your own
_Dead or Alive Xtreme Beach Volleyball_ disc.

1. **Get your DOAXBV disc onto the PC**, either way:
   - a disc image (`.iso`, trimmed or full) dumped from your disc; or
   - its extracted folder, the one holding `datahdd.afs` and
     `datadvd.afs`. If you play DOAXBV through
     [doaxbv-re](https://github.com/NoRain211/doaxbv-re), its imported disc
     is that folder already.
2. **Show the game where it is.** The simplest: put the `.iso` (with
   _xtreme_, _doax_ or _beach_ in its name) or the extracted folder in
   DOA3's `private/` folder. A `doaxbv-re` checkout next to this one
   (`..\doaxbv-re`) is found too, with nothing to copy. Otherwise:
   - **Windows:** drop the `.iso`, the folder or its `datahdd.afs` on the
     launcher window, or open **SWIMSUITS** > **DOAXBV Disc** and choose it;
   - **macOS:** **Choose DOAXBV disc...** in the launcher;
   - **any platform:** write its path on the first line of
     `private/doaxbv-path.txt`, or set `RECOMP_DOAXBV_DISC`.

   The first start remembers the disc it found in `private/doaxbv-path.txt`.
3. **Leave Swimsuits on** in the launcher (**SWIMSUITS** page on Windows; it
   is on by default) and start the game.
4. **In a fight with a woman, click L3** and press **LB** (see
   [In a fight](#in-a-fight)). The first time a suit is chosen it is built
   from the disc, in a few milliseconds.

The suit recipes ship with the port (`recipes/doaxbv-suits.pack.gz`) and
are unpacked to `private/doaxbv-suits.pack` on first start. Without them or
a disc the game is unchanged. If nothing happens on L3, the game log in
`private/play-logs/` should show `recomp costume inject: 2149 suits in ...`;
when that line is missing, the disc was not found.

## In a fight

Everyone starts in her DOA3 costume. Nothing changes until a switch is used.

Click **L3** (press in the left stick) on any controller to open the swimsuit
menu, and click it again to close it. While the menu is open, the
controller's left half sets P1 and its right half P2, and a panel shows both
players' swimsuits with the controls along the bottom of the screen.

| Menu open (**L3**) | P1 (left half) | P2 (right half) |
|---|---|---|
| Next / previous swimsuit | **LB** / **LT** | **RB** / **RT** |
| Mark or unmark a favorite | Tap **X** | Tap **B** |
| Browse only favorites, or every swimsuit again (both players) | **Y** | **Y** |

While the swimsuit switch is on, the stick clicks never reach DOA3. While the
menu is open, that controller's buttons, sticks, d-pad and Start do not
either, so switching never throws a punch, resets training or skips a
cutscene; close the menu to fight again. Back always reaches the game.

| Keyboard | Effect |
|---|---|
| **1** / **2** (**Shift** goes back) | P1 / P2 swimsuit |
| **F** (**Shift**: P2) | Mark P1's swimsuit as a favorite |
| **G** | Browse favorites only, or every swimsuit again |

Holding a switch scrolls: it repeats after a moment and speeds up the longer
it is held. A box under the life bars shows each player's swimsuit number and
name while switching, with `*` after a favorite and `fav` after the player
while browsing favorites only. The list for each fighter runs from her DOA3 costume
through every DOAXBV swimsuit she can wear. A new fighter in a slot starts in
her DOA3 costume.

Favorites are kept per girl in `private/suit-favorites.txt` (one
`<girl> <suit number>` per line) and last between sessions. Tick **Start in a
favorite** in the launcher to have each fighter start in her first favorite
instead of her DOA3 costume.

## How it works

Both games build a character from rigid per-bone pieces and joint pieces, and
both keep models in AFS archives of XPR0 bundles. DOAXBV keeps every suit as
a separate file fitted to each girl's body; its body and suit pieces, their
textures and their vertex weights are all on the disc.

What the disc does not say is how the game puts them together: which bone
each piece hangs on, which body pieces a suit hides, and how the game bends
its CPU-skinned pieces (joints, the chest, tops) each frame. Those were taken
once, offline, from DOAXBV running under doaxbv-re, and reduced to a recipe
per suit: bone bindings and offsets, materials, the chest's bounce mask,
weld lists for the joints, and for every piece where its bytes are on the
disc and the steps that turn them into the suit's (a transform, welds onto a
neighbouring piece, trimming to the vertices used, mirroring, re-binding to
the chest). The pack stores identical pieces once; all seven girls' 2,149
suits take about 4 MB compressed.

At run time the costume injector (`recomp-runtime/costume_inject.c`) loads a
suit when it is chosen: `suit_recipe.c` reads the ranges the recipe names from
the disc image or folder and replays its steps, which takes a few
milliseconds, then the injector draws it in place of the fighter's body
(the most recent twelve suits stay loaded). Ankles, elbows, wrists and toes
are bent each frame exactly as DOAXBV bends them (`suit_joints.c`, with
DOAXBV's own joint maths in `body_joints.c`, from doaxbv-re's
`docs/body-joints.md`): each joint's rings and their frames come from the
girl's body on the disc, where the joint list follows her geometry entry.
`suit_joints_check` checks the reader and the bend against vertices DOAXBV
itself wrote. Hands close into fists and open flat with the fighter's own:
DOAXBV blends each hand between stored shapes, the girl's fist and flat hand
are read from her body, and the suit's hand follows how closed DOA3's hand
is (`suit_hands.c`). The suit's skeleton is retargeted
onto the DOA3 fighter's: DOA3's rotations, DOAXBV's bone lengths. Suit mode
then joins the two heads and bodies:

- **Head offset.** The two games put the head joint at different points of
  the same face. A per-girl offset, fitted by matching the two faces (to
  under 2 mm), seats the DOAXBV neck exactly where the DOA3 face starts.
- **Chest seat.** DOA3 turns the head about a joint fixed on its chest. The
  DOAXBV chest, rebuilt through DOAXBV's own spine, would drift 2-5 cm from
  that joint from frame to frame, so the upper body is placed where DOA3's
  head joint sits in the DOAXBV chest's frame at the bind pose; the waist,
  blended between hip and chest, takes up the difference.
- **Neck.** DOA3's head turns up to 60-80 degrees against the chest. The
  DOAXBV neck is carried from the chest at its base to the head at its top,
  turning about DOA3's head joint rather than sliding, so the top of the neck
  stays on the head (under 3 mm of drift in tests). The first 40 degrees of a
  turn are taken by the neck alone, each vertex by its share of DOAXBV's own
  neck and head bones; only the rest spreads over the top of the chest, so
  the chest stays still for most turns.
- **Straps.** Suit cloth near the neck rides the skin under it instead of
  turning by itself, so straps stay on the neck and stay put on the chest.
- **Skin.** DOAXBV skin wears the fighter's own face material each frame,
  specular included (DOA3 lights its own bodies with it), so the seam matches
  under any stage's lights.
- **Neckwear.** DOA3 chokers, collars and neck skin on the head bone are
  hidden.
- **Chest.** Tops move with the chest as far as they lie over the breast;
  below it they keep DOAXBV's own blend of chest, waist and hip, so
  one-pieces and long tops follow the torso as it bends. The chest and its
  top bounce on a spring fitted to DOAXBV's own chest motion
  (about 3.5 Hz), pushed by the chest's acceleration.
- **Ankles.** When a fighter's toes come up further than DOAXBV's girls ever
  lift theirs (a back foot in a deep stance), the back of the ankle is laid
  on a straight line from the calf to the heel, as a tendon runs, instead of
  kinking, and never comes nearer the shinbone than it sits at rest, so the
  ankle keeps its thickness however far the toes come up (`suit_legs.c`).
- **Skirts.** Skirts move as DOAXBV moves them (`suit_skirt.c`). DOAXBV
  hangs a skirt as a curtain of strands from a ring on the hip; each tick
  every link swings as a damped pendulum toward a flared rest shape, the
  ring of tips keeps its spacing, and the hem is pushed out of ellipsoids
  and capsules on the waist, hip and thighs. The grid's points then become
  the skirt's vertices (pleats at their own offsets from them). Every
  number, collider and vertex comes from the suit's script entry on the
  player's disc, read as the suit is built: nothing about skirts is in the
  recipes. DOAXBV's girls stroll, and a fighter dashes ten times as fast,
  so each tick the skirt is first carried 80% of the way with the hip's
  motion and feels the rest as inertia; lifted thighs and turns act in
  full.
- **Tassels.** Tassels, ties and strings move on DOAXBV's pendulum chains
  (`suit_chain.c`): each hangs from a root on a bone (a tassel from the
  edge of a cup), each link swings, falls and is kept out of the chain's
  capsules, and each node places its ribbon of vertices in a frame along
  the chain. Numbers, colliders and vertices again come from the suit's
  script entry on the disc. A chain on a bouncing cup hangs from the
  bounced cup, and chains are carried only 40% with their bone, so they
  swing more than the skirts.

Fighters are told apart by skeleton (bone lengths); Kasumi and Ayane share
one, so the hair colour of the head draws decides.

## Limits

- Only chains that move a suit piece's vertices are read; DOAXBV's chains
  that draw rigid pieces at their nodes keep their rest shape.
- Past about 30 degrees the neck can crease slightly at the throat, and past
  40 degrees the top of the chest turns a little with it.
- At steep toes-up a slight hump can show behind the ankle, where the
  straightened back of the leg meets the heel.
- Fingers keep DOAXBV's relaxed rest pose.
- Cloth highlights are a little strong under some stages' lights.

## Runtime options

| Variable | Effect |
|---|---|
| `RECOMP_DOAXBV_DISC` | The DOAXBV disc: an `.iso` or a folder with `datahdd.afs` and `datadvd.afs`. |
| `RECOMP_COSTUME_PACK` | The unpacked recipe pack. `run_game.sh` and the Windows launcher unpack `recipes/doaxbv-suits.pack.gz` into `private/`. |
| `RECOMP_COSTUME_FAVORITES` | The favorites file. Default: `suit-favorites.txt` beside the recipe pack. |
| `RECOMP_COSTUME_START_FAVORITE=1` | Each fighter starts in her first favorite (the launcher's **Start in a favorite**). |
| `RECOMP_COSTUME_SKIN_MATCH=0` | Keeps DOAXBV's own skin colour. |
| `RECOMP_CHEST_PLACE=0` | Leaves the chest where DOAXBV's spine puts it. |
| `RECOMP_NECK_FOLLOW=0` | Skins the neck as DOAXBV does, without carrying it to the head. |
| `RECOMP_SKIRT_PHYSICS=0` | Leaves skirts and tassels in their captured shape. |
| `RECOMP_COSTUME_HEAD_UP`, `RECOMP_COSTUME_HEAD_FORWARD` | Override the girl's head offset, in cm. |
