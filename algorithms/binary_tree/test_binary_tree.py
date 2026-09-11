"""Run with: python -m unittest -v"""

import random
import unittest

from binary_tree import BinarySearchTree


class BinaryTreeTests(unittest.TestCase):
    def test_traversals_and_empty_tree(self):
        tree = BinarySearchTree()
        for traverse in (tree.inorder, tree.preorder, tree.postorder, tree.level_order):
            self.assertEqual(traverse(), [])
        self.assertFalse(tree.contains(1))
        self.assertFalse(tree.delete(1))
        tree = BinarySearchTree([8, 3, 10, 1, 6, 14, 4, 7, 13])
        self.assertFalse(tree.insert(6))
        self.assertEqual(tree.inorder(), [1, 3, 4, 6, 7, 8, 10, 13, 14])
        self.assertEqual(tree.preorder(), [8, 3, 1, 6, 4, 7, 10, 14, 13])
        self.assertEqual(tree.postorder(), [1, 4, 7, 6, 3, 13, 14, 10, 8])
        self.assertEqual(tree.level_order(), [8, 3, 10, 1, 6, 14, 4, 7, 13])

    def test_delete_shapes(self):
        for values, target in [
            ([5], 5), ([5, 3], 5), ([5, 7], 5),
            ([5, 3, 7], 3), ([5, 3, 2], 3),
            ([5, 3, 7, 8], 5), ([5, 3, 9, 7, 8], 5),
            ([10, 5, 3, 8, 6, 7, 12], 5),
        ]:
            with self.subTest(values=values, target=target):
                tree = BinarySearchTree(values)
                self.assertTrue(tree.delete(target))
                self.assertEqual(tree.inorder(), sorted(set(values) - {target}))
                self.assertFalse(tree.contains(target))
                self.assertFalse(tree.delete(target))

    def test_random_operations_against_set(self):
        rng, expected, tree = random.Random(42), set(), BinarySearchTree()
        for _ in range(2000):
            value = rng.randrange(-100, 101)
            if rng.randrange(2):
                self.assertEqual(tree.insert(value), value not in expected)
                expected.add(value)
            else:
                self.assertEqual(tree.delete(value), value in expected)
                expected.discard(value)
            self.assertEqual(tree.inorder(), sorted(expected))
            self.assertEqual(tree.contains(value), value in expected)

    def test_deep_tree_without_recursion(self):
        values = list(range(1500))
        tree = BinarySearchTree(values)
        self.assertEqual(tree.inorder(), values)
        self.assertEqual(tree.preorder(), values)
        self.assertEqual(tree.postorder(), values[::-1])
        self.assertEqual(tree.level_order(), values)
        self.assertTrue(tree.contains(1499))
        self.assertTrue(tree.delete(1499))
        self.assertEqual(tree.inorder(), values[:-1])


if __name__ == "__main__":
    unittest.main()
