package com.ebike.xclass.data

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.Query
import kotlinx.coroutines.flow.Flow

@Dao
interface RideDao {
    @Insert
    suspend fun insert(ride: Ride): Long

    @Query("SELECT * FROM rides ORDER BY startedAt DESC")
    fun observeAll(): Flow<List<Ride>>
}
