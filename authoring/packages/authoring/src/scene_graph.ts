import type { AuthoringNode } from "./generated/index.js";
import { AuthoringError } from "./errors.js";

export type ReparentOptions = {
  /** Index among siblings under the destination parent; omit to append. */
  readonly siblingIndex?: number;
};

function cloneNode(node: AuthoringNode): AuthoringNode {
  const next: AuthoringNode = { ...node };
  if (node.children !== undefined) {
    next.children = node.children.map(cloneNode);
  } else {
    delete next.children;
  }
  return next;
}

function cloneRoots(roots: readonly AuthoringNode[]): AuthoringNode[] {
  return roots.map(cloneNode);
}

function subtreeContainsId(node: AuthoringNode, id: string): boolean {
  if (node.id === id) {
    return true;
  }
  for (const child of node.children ?? []) {
    if (subtreeContainsId(child, id)) {
      return true;
    }
  }
  return false;
}

type Locate = {
  siblings: AuthoringNode[];
  index: number;
};

function locateNode(nodes: AuthoringNode[], id: string): Locate | undefined {
  for (let index = 0; index < nodes.length; ++index) {
    if (nodes[index]!.id === id) {
      return { siblings: nodes, index };
    }
    const nested = locateNode(nodes[index]!.children ?? [], id);
    if (nested) {
      return nested;
    }
  }
  return undefined;
}

function findNode(nodes: AuthoringNode[], id: string): AuthoringNode | undefined {
  for (const node of nodes) {
    if (node.id === id) {
      return node;
    }
    const nested = findNode(node.children ?? [], id);
    if (nested) {
      return nested;
    }
  }
  return undefined;
}

/** Depth-first node ids in document order. */
export function collectNodeIds(roots: readonly AuthoringNode[]): string[] {
  const ids: string[] = [];
  const walk = (node: AuthoringNode): void => {
    ids.push(node.id);
    for (const child of node.children ?? []) {
      walk(child);
    }
  };
  for (const root of roots) {
    walk(root);
  }
  return ids;
}

/**
 * Immediate parent id, or null at shot root. Returns undefined when missing.
 */
export function parentIdOf(
  roots: readonly AuthoringNode[],
  nodeId: string,
): string | null | undefined {
  const walk = (
    nodes: readonly AuthoringNode[],
    parent: string | null,
  ): string | null | undefined => {
    for (const node of nodes) {
      if (node.id === nodeId) {
        return parent;
      }
      const nested = walk(node.children ?? [], node.id);
      if (nested !== undefined) {
        return nested;
      }
    }
    return undefined;
  };
  return walk(roots, null);
}

/**
 * Move a node (with its nested children) under a new parent or to the root
 * list. Pure authoring data: no UI, DOM, or renderer dependency. Throws on
 * unknown ids or cycle-forming moves. Returns a deep-cloned tree.
 */
export function reparentNode(
  roots: readonly AuthoringNode[],
  nodeId: string,
  newParentId: string | null,
  options: ReparentOptions = {},
): AuthoringNode[] {
  if (!nodeId) {
    throw new AuthoringError("invalid_argument", "nodeId must be non-empty", {
      path: "nodeId",
    });
  }
  if (newParentId !== null && newParentId === "") {
    throw new AuthoringError(
      "invalid_argument",
      "newParentId must be non-empty",
      { path: "newParentId" },
    );
  }
  if (newParentId === nodeId) {
    throw new AuthoringError(
      "cyclic_parent",
      "cyclic_parent: a node cannot be its own parent",
      { path: "newParentId" },
    );
  }

  const next = cloneRoots(roots);
  const source = locateNode(next, nodeId);
  if (!source) {
    throw new AuthoringError("unknown_node", `unknown_node: ${nodeId}`, {
      path: "nodeId",
    });
  }

  let destination: AuthoringNode[] = next;
  if (newParentId !== null) {
    const parent = findNode(next, newParentId);
    if (!parent) {
      throw new AuthoringError(
        "unknown_parent",
        `unknown_parent: ${newParentId}`,
        { path: "newParentId" },
      );
    }
    if (subtreeContainsId(source.siblings[source.index]!, newParentId)) {
      throw new AuthoringError(
        "cyclic_parent",
        "cyclic_parent: cannot reparent a node under its own descendant",
        { path: "newParentId" },
      );
    }
    if (!parent.children) {
      parent.children = [];
    }
    destination = parent.children;
  }

  let insertAt = destination.length;
  if (options.siblingIndex !== undefined) {
    if (
      !Number.isInteger(options.siblingIndex) ||
      options.siblingIndex < 0 ||
      options.siblingIndex > destination.length
    ) {
      throw new AuthoringError("range_error", "siblingIndex out of range", {
        path: "siblingIndex",
      });
    }
    insertAt = options.siblingIndex;
  }

  if (destination === source.siblings && insertAt > source.index) {
    --insertAt;
  }
  if (
    destination === source.siblings &&
    insertAt === source.index &&
    options.siblingIndex !== undefined
  ) {
    return next;
  }

  const [moved] = source.siblings.splice(source.index, 1);
  if (!moved) {
    throw new AuthoringError("unknown_node", `unknown_node: ${nodeId}`, {
      path: "nodeId",
    });
  }
  if (insertAt > destination.length) {
    insertAt = destination.length;
  }
  destination.splice(insertAt, 0, moved);
  return next;
}
