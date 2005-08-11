package gfront;
import javax.swing.tree.*;

public class DirectoryTreeNode extends DefaultMutableTreeNode {
	protected boolean _isExplored = false;

	public DirectoryTreeNode() {
		super();
	}

	public boolean isLeaf() {
		return false;
	}

	public void setExplored(boolean b) {
		_isExplored = b;
	}

	public boolean isExplored() {
		return _isExplored;
	}
}
