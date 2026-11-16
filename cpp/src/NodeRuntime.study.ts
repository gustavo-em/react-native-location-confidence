// Study file helper. Not part of the library.
//
// The study files end with their examples inside `if (import.meta.main)`.
// Node sets `import.meta.main` to true only in the file that was run
// directly, so the examples of a file do not run when another study file
// imports it. TypeScript does not know about that property in this project,
// so this file tells it.

declare global {
  interface ImportMeta {
    readonly main: boolean;
  }
}

export {};
