# C++ style

FACT This file is the director's C++ style. Each RULE names its gate: `.clang-format`
(`scripts/check-format.py`), `.clang-tidy` (`scripts/check-tidy.py`), both LLVM 22
(`docs/build.md`), or `none: judgement` for the reviewer.
source: director

## The example

The style in one place, as `.clang-format` writes it (names aligned, braces not; see "Limits of the
tools").

```
const Creature& creature { kInvalidCreature };
const char*     lookupName {};

if (! isValidCreatureName(lookupName))
    return error(...);

if (auto creap = findCreature(lookupName); creap == nullptr)
    return NotFoundError(...);
else
{
    auto result = ...(creap);
    return success(result. ...);
}
```

## Layout

RULE Indent is 4 spaces, never tabs: ts=4 sw=4 et. It applies to every C++ file and to CMake files.
source: director
enforced_by: .clang-format (IndentWidth: 4, TabWidth: 4, UseTab: Never; `AccessModifierOffset: -4`) for C++; CMake files none: judgement

RULE Braces are Allman ("non-Egyptian"): the opening brace is on its own line. A single-statement
`if` body may be left unbraced on the next line.
source: director
enforced_by: .clang-format (BreakBeforeBraces: Allman, AllowShortIfStatementsOnASingleLine: Never)

RULE An empty brace pair is written `{}`: `const char* lookupName {};`, `std::errc {}`. A non-empty
braced list keeps its inner spaces (`{ x }`).
source: director
enforced_by: .clang-format (SpaceInEmptyBlock: false, Cpp11BracedListStyle: false)

RULE Write few operations per line. One statement per line, one declaration per statement.
source: director
enforced_by: .clang-format (statements) and .clang-tidy readability-isolate-declaration; the number of operations per line is none: judgement

RULE Align declarations, names and initialisers vertically in a run of consecutive lines.
source: director
enforced_by: .clang-format (AlignConsecutiveDeclarations, AlignConsecutiveAssignments)

RULE Initialise with braces: `int count { 0 };`, not `int count = 0;` and not `int count(0);`.
source: director
enforced_by: .clang-format (spacing of `{ }`); the choice of braces over `=` is none: judgement

## Const and types

RULE `const` goes west, left of the type: `const Creature* cre`. Const hygiene is strict: every
variable, parameter, reference and pointee that is not modified is const.
source: director
enforced_by: .clang-format (QualifierAlignment: Left), .clang-tidy misc-const-correctness (values, references, pointers, pointees)

RULE Pointer and reference sigils hug the type: `const char* name`, `Creature& creature`.
source: director
enforced_by: .clang-format (PointerAlignment: Left, ReferenceAlignment: Left)

## Names

RULE Functions, methods, variables and parameters are camelCase. Types, aliases, template
parameters and concepts are PascalCase. Namespaces are lower_case. Macros are UPPER_CASE.
source: director
enforced_by: .clang-tidy readability-identifier-naming

RULE camelCase applies to source files and to the names MuckaII defines in CMake. C++ files are
`mudTests.cpp`, not `mud_tests.cpp`. CMake targets, functions and variables are camelCase:
`muckaiiMud`, `muckaiiMudTests`, `muckaiiTui`, `muckaiiCheckLayers()`. Documentation file names
(`architecture.md`) keep their own form. Code identifiers written in documentation follow this
file (`submitLine`, `mud::Decoder`). Data formats keep their own naming: JSON field names in
`events.md`, TOML keys.
source: director
enforced_by: none: judgement

RULE Data members carry an `m` prefix (`mName`). True constants carry a `k` prefix
(`kInvalidCreature`): constexpr variables, namespace-scope and static constants, class constants
and enumerators. A const local or const parameter is an ordinary camelCase variable
(`const Creature& creature`), not a `k` constant.
source: director
enforced_by: .clang-tidy readability-identifier-naming (MemberPrefix, ConstantMemberPrefix, ConstexprVariablePrefix, GlobalConstantPrefix, StaticConstantPrefix, ClassConstantPrefix, EnumConstantPrefix)

RULE A predicate is named with `is`, `has`, `can` or `will`: `isValid`, `hasMoved`, `canFlee`,
`willSleep`.
source: director
enforced_by: none: judgement

RULE `find...` is a failable search and returns a nullable pointer or a `std::optional`.
`get...` cannot fail and returns a value or a reference. A `get` that can fail is a bug.
source: director
enforced_by: none: judgement

RULE Write negation with a space after the `!`: `! isValid(x)`.
source: director
enforced_by: .clang-format (SpaceAfterLogicalNot: true)

## Errors

RULE There are no exceptions. The build disables them (`/EHs-c-` and `_HAS_EXCEPTIONS=0` on MSVC,
`-fno-exceptions` on clang), so `throw`, `try` and `catch` do not compile. Report failure with
a non-throwing error code or argument in the `std::from_chars` style (`std::errc`, a result struct),
or with a structured return (`std::optional`, an expected type).
source: director
enforced_by: CMake target flags in cmake/MuckaiiTargets.cmake (a throw fails the build)

RULE Use `static_assert` wherever a compile-time fact can be checked: sizes, constants,
`noexcept`, trivially-copyable-ness, table lengths.
source: director
enforced_by: none: judgement

RULE No raw owning `new` or `delete`, no `malloc`, no C-style casts. Ownership is a value, a
`std::unique_ptr` or a container.
source: director
enforced_by: .clang-tidy cppcoreguidelines-owning-memory, cppcoreguidelines-no-malloc, cppcoreguidelines-pro-type-cstyle-cast, google-readability-casting

## Design

RULE Design in the Go manner: small interfaces, composition over inheritance, plain structs, explicit
error values, early returns. Check the failure case first and return; keep the success path at the
left margin.
source: director
enforced_by: none: judgement

## Tests

RULE Every library has a unit-test target, `muckaii<Layer>Tests` (`docs/build.md`, Targets), and a
new function arrives with its tests. Coverage tooling is a later pass.
source: director
enforced_by: cmake/MuckaiiLayers.cmake (`muckaiiCheckLayers` fails the configure when a layer has no test target); whether each function has a test is none: judgement

## Limits of the tools

FACT Where a tool cannot say the rule, the rule stays and the gap is recorded here. No tool is
bent to fake it.
source: director

| Rule | What the tool does instead |
|------|---------------------------|
| Aligning the `{` of consecutive braced initialisers (`creature   {` / `lookupName {`) | clang-format aligns the declared names and the `=` of assignments, not a braced list. It writes `const Creature& creature { kInvalidCreature };` with one space. Aligned names are enforced; aligned braces are not |
| `=` versus braces for initialisation | no clang-tidy check demands braces for all variables; `modernize-use-default-member-init` and `cppcoreguidelines-pro-type-member-init` cover members only |
| Predicate prefixes `is`, `has`, `can`, `will` | `readability-identifier-naming` cannot key a prefix on a return type |
| `find` versus `get` | not a naming-case rule; no check inspects whether the return is failable |
| Few operations per line | no tool measures it |
| One-statement `if` body unbraced | allowed, not required: the tools accept both forms, and `readability-braces-around-statements` is off on purpose |
| Early returns, small interfaces | no tool measures them |
| Test exists for every function | no coverage tool yet |

## Where the gates run

FACT `scripts/check-format.py` runs in `scripts/check-all.py`. `scripts/check-tidy.py` runs from
`ctest` as `repoTidy` (it takes about 15 seconds). Both fail, rather than skip, when LLVM 22 is not
installed. `clang-format -i <file>` fixes a formatting failure.
source: agent
