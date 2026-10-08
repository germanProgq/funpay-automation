# FunPay Vertex

A pricing and inventory tool for FunPay sellers, built in C++17. It keeps listings priced against competitors, tracks price history, and manages catalogs for a team or an organization.

The project is a work in progress. It is organized as a native core library with a GTK desktop app on top, a small web frontend, and a set of database tools.

## What is inside

- `fpv_core`: the core library. Data model, pricing rules, and repository and service code backed by Postgres.
- `fpv_app`: a GTK desktop application that uses the core.
- `valorant_client`: a small companion client that talks to game account endpoints.
- `web`: a static web frontend (plain HTML, CSS, and JS).
- `tools`: database helpers for migrate, seed, backup, and restore, plus a small site server.
- `tests`: CTest-based tests for the core.

## Data model

The catalog is multi-tenant. The main entities are Organization, Team, User, and Role on the account side, and Item, Listing, CompetitorListing, PriceRule, and PriceHistory on the catalog side. Audit logs and retention rules are tracked alongside them.

## Build

You need CMake 3.24 or newer, a C++17 compiler, and the GTK development packages for the desktop app. Postgres is used for storage.

```sh
cmake -S . -B build-gtk
cmake --build build-gtk --target fpv_app
./build-gtk/fpv_app/fpv_app
```

Build options you can pass to CMake:

| Option | Default | Builds |
| --- | --- | --- |
| `FPV_BUILD_APP` | ON | The GTK application |
| `FPV_BUILD_VALORANT_CLIENT` | ON | The companion client |
| `FPV_BUILD_TESTS` | ON | The test suite |
| `FPV_BUILD_TOOLS` | ON | The database helper tools |

Run the tests with `ctest` from the build directory once they are built.

## Docker

A `Dockerfile` and `docker-compose.yml` are included for running the stack in containers.

## Notes

Keep database credentials and any account details in environment variables or a local config that is not committed. This is a personal project and the interfaces may still change.
