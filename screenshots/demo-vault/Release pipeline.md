# Release pipeline

```mermaid
flowchart TD
    A[Commit] --> B[CI build]
    B --> C[Unit suite]
    B --> D[Packaging]
    C --> E{Gates green?}
    D --> E
    E -->|yes| F[Draft release]
    E -->|no| G[Fix and retag]
```
