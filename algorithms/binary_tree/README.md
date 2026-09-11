# Binary tree algorithms

A dependency-free Python 3.9+ binary search tree for integer values, with
insertion, search, deletion, and four iterative traversals. A binary search
tree is a binary tree whose left subtree contains smaller values and whose
right subtree contains larger values. Duplicate insertions are ignored.

```python
from binary_tree import BinarySearchTree

tree = BinarySearchTree([8, 3, 10, 1, 6])
tree.insert(4)               # True
tree.contains(6)             # True
tree.inorder()               # [1, 3, 4, 6, 8, 10]
tree.delete(3)               # True
tree.level_order()           # [8, 4, 10, 1, 6]
```

Run the example and tests from this directory:

```sh
python binary_tree.py
python -m unittest -v
```

`insert` and `delete` return whether the tree changed. `contains` returns a
boolean. Traversals return lists; an empty tree returns an empty list.

| Operation | Time | Auxiliary space, excluding output |
| --- | --- | --- |
| Insert, search, delete | O(h) | O(1) |
| Inorder, preorder, postorder | O(n) | O(h) |
| Level order | O(n) | O(w) |

Here `n` is the node count, `h` is the tree height, and `w` is its maximum
width. The tree is not self-balancing: sorted input can make `h = n`, so
individual updates and searches become O(n), and building the tree can take
O(n²). Every traversal also allocates an O(n) result list. Iterative algorithms
avoid Python's recursion-depth limit on deep trees.

Tests cover traversal order, empty trees, duplicates, deletion shapes,
2,000 deterministic random operations against a Python set, and a 1,500-node
skewed tree.
