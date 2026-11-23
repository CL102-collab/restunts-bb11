# Chocolate Stunts

Website: https://commonloon102.github.io/restunts-bb11/

Vanilla, Chocolate and SDL port for Stunts 1.1 (Brøderbund).

For the vanilla source without any fixes or added features,
switch to the `vanilla-stunts` branch.  
It is 16-bit DOS only.

This project is based on Restunts: https://wiki.stunts.hu/wiki/Restunts

Initially forked from this repository: https://github.com/4d-stunts/restunts

## Runnable packages
Go to the website and find them at the downloads section:  
https://commonloon102.github.io/restunts-bb11/#downloads

All packages were created by GitHub CI and released as an immutable release directly from the pipeline, so it is guaranteed that the binaries are built directly from the source available on GitHub.

## AI usage
It's not a secret, AI (GPT-5.6 Sol, GPT-6 Astra, GPT-6.1 Sol) was used for the following:
- Writing code
- Reviewing code
- Manual testing
- Writing documentation
- Writing automatic tests
- Writing the CI pipeline
- Upscaling some pictures and backgrounds
- Generating the two new cars (Tesla Roadster Rocket and McMurtry Spéirling PURE)

## How quality is ensured?
I've collected 7000 replays from the past 25 years and ran them through with the original game and with the newly built one. Every frame of the ~190 hours of gameplay is producing the same pixel output. This check is running for every PR and release pipelines also.

I've also done a huge amount of manual testing myself, to test everything else what was not having automated tests.

## How to use the documentation?

The documentation is 100% AI generated.
It's not slop, no hallucinations, and it is up to date.
However, who likes to read documentation? The answer is: AI.
So, you won't read it anyway (this paragraph is intended for
humans, AIs can ignore it). Even if you want to know something
about the game, you will give the GitHub link to an AI agent and
ask your questions and it will find the answers in the .md files
and in the source code.

Surprise: the docs are in the docs folder.
Some starting points:

- [Documentation for users](docs/users.md)
- [Documentation for developers](docs/devs.md)
