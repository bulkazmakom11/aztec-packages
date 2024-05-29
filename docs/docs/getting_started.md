---
title: Quickstart
---

The easiest way to start developing on Aztec locally is through `npx aztec-app`. This is a convenient way of installing the development environment (A.K.A. Sandbox) and starting new projects from a boilerplate.

If you'd like to develop remotely (for example, if you're on Windows or have trouble working with Docker), follow the [codespaces guide](./getting_started/codespaces.md).

## Prerequisites

- Node.js >= v18 (recommend installing with [nvm](https://github.com/nvm-sh/nvm))
- Docker (visit [this page of the Docker docs](https://docs.docker.com/get-docker/) on how to install it)

### Run the `npx` script

Thanks to Node, you can run the recommended `npx script`:

```bash
npx aztec-app
```

This script gives you some options to bootstrap a new project, start/stop the sandbox, or see the logs. Run `npx aztec-app -h` for a list of options.

## Codespaces

As easy as it is, you may not want to run the sandbox locally. We thought about you exactly ❤️ So you can use prebuilt codespaces: remote servers running everything for you. They're free and fast to use, and we're big fans!

Just click one of these buttons:

[![One-Click React Starter](/img/codespaces_badges/react_cta_badge.svg)](https://codespaces.new/AztecProtocol/aztec-packages?devcontainer_path=.devcontainer%2Freact%2Fdevcontainer.json) [![One-Click HTML/TS Starter](/img/codespaces_badges/vanilla_cta_badge.svg)](https://codespaces.new/AztecProtocol/aztec-packages?devcontainer_path=.devcontainer%2Fvanilla%2Fdevcontainer.json) [![One-Click Token Starter](/img/codespaces_badges/token_cta_badge.svg)](https://codespaces.new/AztecProtocol/aztec-packages?devcontainer_path=.devcontainer%2Ftoken%2Fdevcontainer.json)

That's it!

This creates a codespace with a prebuilt image containing one of the "Aztec Boxes" and a development network (sandbox). 
- You can develop directly on the codespace, push it to a repo, make yourself at home.
- You can also just use the sandbox that comes with it. The URL will be logged, you just need to use it as your `PXE_URL`.

## Develop Locally

You can have some more control over the sandbox by installing it manually through the underlying script used by the above methods. [Follow the reference for more info on it](./reference/sandbox_reference/index.md)

## What's next?

Now you have a development network running,, so you're ready to start coding your first app with Aztec.nr and Aztec.js!

To follow the series of tutorials, start with the private voting contract [here](./tutorials/private_voting_contract.md).

If you want to just keep learning, you can read about the high level architecture on the [Core Components page](./aztec/concepts/state_model/index.md) and [the lifecycle of a transaction](./aztec/concepts/transactions.md).

