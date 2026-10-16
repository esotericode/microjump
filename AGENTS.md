# microjump

A 3D platformer running on a game engine we build ourselves in C++.

This file guides the AI agents working on microjump: Claude, ChatGPT and any others who
join. It is deliberately short. It says what the project is, and then leaves the
choices to you.

## What microjump is

- **A custom engine in C++.** The engine is ours: rendering, collision, movement,
  camera, the main loop, resources, settings and tools. Libraries are fine for
  plumbing, such as windows, input, loading files and GPU memory. An existing game engine
  is not.
- **A 3D platformer.** It is third-person, with movement that is expressive and rewards
  skill: momentum you build on purpose, jumps, wall kicks, long jumps, and a camera that
  feels good. It is set in places that are a joy to explore.
- **MIT licensed.** Stick to dependencies under permissive licences.

## What you do here

Improve the game in ways you choose. No one hands you a task list. Look at the project,
play it in your head (or for real), decide what would make it better, and do that.
Think for yourself, and follow what excites you.

"Improvement" can mean many things, for example:

- **Content.** This may matter most: better levels, more levels, larger environments,
  expanded areas, collectables, secrets, things to do and reasons to explore.
- **Gameplay.** New moves, better feel, challenges, bug fixes.
- **Graphics.** Lighting, materials, effects, atmosphere.
- **Engine.** New capabilities, robustness, tools that unlock more of the above.
- **Interface.** Menus, settings, UI that makes the game nicer to use.

These are examples, not a menu.

### Make it count

Choose work with real, visible consequences, things a player would notice or that open
up what comes next. Avoid bikeshedding: don't polish what doesn't matter, rename for
the sake of it, or pick a task because it looks easy. Before you start, ask yourself
whether the game will be meaningfully better when you are done.

## Work together

[`agentcollab.md`](agentcollab.md) is the shared pad where agents talk to each other.
Use it: share plans before big changes, debate ideas, review each other's work, ask for
help, offer it, cheer each other on, and leave notes for whoever comes next.
Collaboration matters here as much as code. The pad starts with its own guidance on how
to talk there.

## A few practical things

- Work on a branch and open a pull request that describes what changed and what you saw
  it do. You may push your branches and merge your PRs when the checks pass.
- Keep the game building and playable on both Linux and Windows; the owner plays on
  Windows.
- Be honest about what works and what doesn't.
- `README.md` explains how to build and run. `docs/` describes how the engine,
  movement, levels and tests work; keep it current when you change them.
  [`docs/history/`](docs/history/) holds the previous charter. Read it as background,
  not as rules.
