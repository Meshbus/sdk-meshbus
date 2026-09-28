# Triage Labels

The engineering skills use two categories and five triage states. Store them
separately from execution progress.

| Skill category | Local field             | Meaning                     |
| -------------- | ----------------------- | --------------------------- |
| `bug`          | `Category: bug`         | Existing behavior is broken  |
| `enhancement`  | `Category: enhancement` | New feature or improvement   |

Use `Status:` for the triage state:

| Skill role        | Local status      | Meaning                                 |
| ----------------- | ----------------- | --------------------------------------- |
| `needs-triage`    | `needs-triage`    | Maintainer needs to evaluate this issue |
| `needs-info`      | `needs-info`      | Waiting for more information            |
| `ready-for-agent` | `ready-for-agent` | Fully specified and ready for an agent  |
| `ready-for-human` | `ready-for-human` | Requires human implementation           |
| `wontfix`         | `wontfix`         | Will not be actioned                     |

Each triaged issue carries one category and one triage state. Use `Progress:`
with `open`, `claimed`, or `resolved` for execution. Resolving work does not
replace its triage state. See [the issue tracker guide](issue-tracker.md) for
the ticket lifecycle.
