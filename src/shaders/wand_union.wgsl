// Lock-free union-find over labels (Playne and Hawick). A label is a pixel index no
// greater than its own, so roots are the smallest index of their component; parents
// only decrease, which keeps concurrent finds and path halving valid.
fn find(start: u32) -> u32 {
    var node = start;
    var parent = atomicLoad(&labels[node]);
    while (parent != node) {
        // Path halving: point the node at its grandparent, an ancestor no larger.
        let grandparent = atomicLoad(&labels[parent]);
        if (grandparent != parent) { atomicMin(&labels[node], grandparent); }
        node = grandparent;
        parent = atomicLoad(&labels[node]);
    }
    return node;
}

fn unite(first: u32, second: u32) {
    var a = find(first);
    var b = find(second);
    loop {
        if (a == b) { return; }
        if (a < b) {
            let previous = atomicMin(&labels[b], a);
            if (previous == b) { return; }
            b = find(previous);
        } else {
            let previous = atomicMin(&labels[a], b);
            if (previous == a) { return; }
            a = find(previous);
        }
    }
}
