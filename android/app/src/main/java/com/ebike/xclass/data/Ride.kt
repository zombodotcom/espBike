package com.ebike.xclass.data

import androidx.room.Entity
import androidx.room.PrimaryKey

/** One logged ride. Distance/efficiency are derived on read in the UI layer. */
@Entity(tableName = "rides")
data class Ride(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val startedAt: Long,
    val endedAt: Long,
    val whUsed: Float,
    val peakWatts: Int,
    val maxSpeedX100: Int,
)
