# Format DATETIME as rfc-3339
def datetime_isoformat(dtstr):
    import datetime
    dt = datetime.datetime.strptime(dtstr, '%Y%m%d%H%M%S')
    return dt.isoformat(timespec='seconds')

DATETIME_ISOFORMAT="${@datetime_isoformat("${DATETIME}")}"
DATETIME_ISOFORMAT[vardepsexclude] = "DATETIME"
