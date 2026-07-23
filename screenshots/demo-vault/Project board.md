# Project board

Live view over the front-matter of everything in `projects/`:

```query
from: projects/
where: status = active
view: table
columns: title, status, priority, due
sort: priority asc
```
