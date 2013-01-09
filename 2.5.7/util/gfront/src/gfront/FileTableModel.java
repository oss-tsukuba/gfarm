package gfront;

public class FileTableModel extends ReadOnlyTableModel {
	public Class getColumnClass(int idx) {
		Class ret = null;
		switch (idx) {
			case 0 :
				ret = String.class;
				break;
			case 1 :
				ret = Number.class;
				break;
			case 2 :
				ret = String.class;
				break;
			case 3 :
				ret = Number.class;
				break;
			default :
				ret = Object.class;
		}
		return ret;
	}
}
