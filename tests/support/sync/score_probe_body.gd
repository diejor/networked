## Entity root probe for on-demand property replication under impaired links.
##
## Carries one integer the authority advances monotonically and pushes with
## [method Netw.sync_property], so a receiver can assert the value never moves
## backward however the link reorders, duplicates, or drops datagrams. Extends
## [StateSyncBody] so the pair also carries the derived position state stream
## the loss and reorder rig drives.
extends StateSyncBody

var score := 0
