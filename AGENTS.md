# AGENTS.md

Entry point for AI assistants. Read in order: [docs/ai/README_AI.md](docs/ai/README_AI.md) → the files it lists.

## Hard rules

1. Never commit game data (extracted files, archives, executables) or excerpts of it beyond what a format spec needs to explain a field.
2. Never write the user's real install path, home dirs or other personal data; use `<BF1942_DIR>`.
3. Tools: Python 3 standard library only ([ADR 0002](docs/architecture/adr/0002-python-tools-cpp-engine.md)). Engine: C++17, CMake, SDL2, OpenGL 3.3 ([ADR 0003](docs/architecture/adr/0003-engine-stack.md)), Jolt Physics ([ADR 0004](docs/architecture/adr/0004-jolt-physics.md)); respect [boundaries](docs/architecture/boundaries.md).
4. A change that makes a doc wrong updates that doc in the same commit; finished work adds a stage to [PROJECT_MEMORY](docs/ai/PROJECT_MEMORY.md).
5. Before committing: the quality bar in [DEVELOPMENT_GUIDE](docs/ai/DEVELOPMENT_GUIDE.md#quality-bar) passes.
6. Mark unverified format claims as such; promote them only after checking against retail data.
