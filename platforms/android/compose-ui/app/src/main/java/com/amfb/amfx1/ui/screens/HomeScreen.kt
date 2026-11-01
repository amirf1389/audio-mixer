package com.amfb.amfx1.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.amfb.amfx1.config.AppConfig
import com.amfb.amfx1.ui.components.HomeTile

// The start page: a title and one big, plain-language choice per task. The choices come from AppConfig.HOME_TILES.
@Composable
fun HomeScreen(onChoose: (tabId: String) -> Unit) {
    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 20.dp, vertical = 24.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Text("Welcome to ${AppConfig.PRODUCT_NAME}", fontSize = 28.sp, fontWeight = FontWeight.ExtraBold, color = MaterialTheme.colorScheme.onBackground)
            Text("Choose what you want to do. You can change anything later.", fontSize = 16.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(6.dp))
            AppConfig.HOME_TILES.forEach { choice ->
                HomeTile(
                    title = choice.title,
                    description = choice.description,
                    onClick = { onChoose(choice.tabId) },
                )
            }
            Spacer(Modifier.height(8.dp))
            Text("${AppConfig.PRODUCT_NAME} by ${AppConfig.COMPANY} · ${AppConfig.VERSION_LABEL}", fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
