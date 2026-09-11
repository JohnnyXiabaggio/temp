"""Integer binary search tree. Run this file for a usage example.

Duplicate values are ignored. Operations use O(h) time, where h is the tree
height; traversals use O(n) time. All algorithms are iterative.
"""

from collections import deque
from dataclasses import dataclass
from typing import Iterable, Optional


@dataclass
class Node:
    value: int
    left: Optional["Node"] = None
    right: Optional["Node"] = None


class BinarySearchTree:
    # ponytail: unbalanced trees can reach O(n) height; use AVL balancing if needed.
    def __init__(self, values: Iterable[int] = ()):
        self.root: Optional[Node] = None
        for value in values:
            self.insert(value)

    def insert(self, value: int) -> bool:
        """Insert a value; return False if it already exists."""
        if self.root is None:
            self.root = Node(value)
            return True
        node = self.root
        while True:
            if value == node.value:
                return False
            side = "left" if value < node.value else "right"
            child = getattr(node, side)
            if child is None:
                setattr(node, side, Node(value))
                return True
            node = child

    def contains(self, value: int) -> bool:
        """Return whether value is present."""
        node = self.root
        while node is not None:
            if value == node.value:
                return True
            node = node.left if value < node.value else node.right
        return False

    def delete(self, value: int) -> bool:
        """Delete a value; return False if absent."""
        parent, node = None, self.root
        while node is not None and node.value != value:
            parent, node = node, node.left if value < node.value else node.right
        if node is None:
            return False
        if node.left is not None and node.right is not None:
            successor_parent, successor = node, node.right
            while successor.left is not None:
                successor_parent, successor = successor, successor.left
            node.value = successor.value
            parent, node = successor_parent, successor
        child = node.left if node.left is not None else node.right
        if parent is None:
            self.root = child
        elif parent.left is node:
            parent.left = child
        else:
            parent.right = child
        return True

    def inorder(self) -> list[int]:
        """Visit left, root, right; returns values in ascending order."""
        result, stack = [], []
        node = self.root
        while node is not None or stack:
            while node is not None:
                stack.append(node)
                node = node.left
            node = stack.pop()
            result.append(node.value)
            node = node.right
        return result

    def preorder(self) -> list[int]:
        """Visit root, left, right."""
        result = []
        stack = [self.root] if self.root is not None else []
        while stack:
            node = stack.pop()
            result.append(node.value)
            if node.right is not None:
                stack.append(node.right)
            if node.left is not None:
                stack.append(node.left)
        return result

    def postorder(self) -> list[int]:
        """Visit left, right, root."""
        result = []
        stack = [self.root] if self.root is not None else []
        while stack:
            node = stack.pop()
            result.append(node.value)
            if node.left is not None:
                stack.append(node.left)
            if node.right is not None:
                stack.append(node.right)
        result.reverse()
        return result

    def level_order(self) -> list[int]:
        """Visit each level from left to right (breadth-first search)."""
        result = []
        queue = deque([self.root] if self.root is not None else [])
        while queue:
            node = queue.popleft()
            result.append(node.value)
            if node.left is not None:
                queue.append(node.left)
            if node.right is not None:
                queue.append(node.right)
        return result


if __name__ == "__main__":
    tree = BinarySearchTree([8, 3, 10, 1, 6, 14, 4, 7, 13])
    print("Inorder:    ", tree.inorder())
    print("Preorder:   ", tree.preorder())
    print("Postorder:  ", tree.postorder())
    print("Level order:", tree.level_order())
    print("Contains 6: ", tree.contains(6))
    tree.delete(3)
    print("Delete 3:   ", tree.inorder())
